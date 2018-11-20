/*
 * Copyright 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "Swappy.h"

#define LOG_TAG "Swappy"

#include <thread>

#include "Log.h"
#include "Settings.h"
#include "Trace.h"

#include "ChoreographerFilter.h"
#include "ChoreographerThread.h"
#include "EGL.h"

using std::chrono::milliseconds;
using std::chrono::nanoseconds;
using namespace std::chrono_literals;

std::mutex Swappy::sInstanceMutex;
std::unique_ptr<Swappy> Swappy::sInstance;

void Swappy::init(JNIEnv *env, jobject jactivity) {
    jclass activityClass = env->FindClass("android/app/NativeActivity");
    jclass windowManagerClass = env->FindClass("android/view/WindowManager");
    jclass displayClass = env->FindClass("android/view/Display");

    jmethodID getWindowManager = env->GetMethodID(
            activityClass,
            "getWindowManager",
            "()Landroid/view/WindowManager;");

    jmethodID getDefaultDisplay = env->GetMethodID(
            windowManagerClass,
            "getDefaultDisplay",
            "()Landroid/view/Display;");

    jobject wm = env->CallObjectMethod(jactivity, getWindowManager);
    jobject display = env->CallObjectMethod(wm, getDefaultDisplay);

    jmethodID getRefreshRate = env->GetMethodID(
            displayClass,
            "getRefreshRate",
            "()F");

    const float refreshRateHz = env->CallFloatMethod(display, getRefreshRate);

    jmethodID getAppVsyncOffsetNanos = env->GetMethodID(
            displayClass,
            "getAppVsyncOffsetNanos", "()J");

    const long appVsyncOffsetNanos = env->CallLongMethod(display, getAppVsyncOffsetNanos);

    jmethodID getPresentationDeadlineNanos = env->GetMethodID(
            displayClass,
            "getPresentationDeadlineNanos",
            "()J");

    const long vsyncPresentationDeadlineNanos = env->CallLongMethod(
            display,
            getPresentationDeadlineNanos);

    const long ONE_MS_IN_NS = 1000000;
    const long ONE_S_IN_NS = ONE_MS_IN_NS * 1000;

    const long vsyncPeriodNanos = static_cast<long>(ONE_S_IN_NS / refreshRateHz);
    const long sfVsyncOffsetNanos =
            vsyncPeriodNanos - (vsyncPresentationDeadlineNanos - ONE_MS_IN_NS);

    using std::chrono::nanoseconds;
    Swappy::init(
            nanoseconds(vsyncPeriodNanos),
            nanoseconds(appVsyncOffsetNanos),
            nanoseconds(sfVsyncOffsetNanos));
}

void Swappy::init(nanoseconds refreshPeriod, nanoseconds appOffset, nanoseconds sfOffset) {
    std::lock_guard<std::mutex> lock(sInstanceMutex);
    if (sInstance) {
        ALOGE("Attempted to initialize Swappy twice");
        return;
    }
    sInstance = std::make_unique<Swappy>(refreshPeriod, appOffset, sfOffset, ConstructorTag{});
}

void Swappy::onChoreographer(void* data) {
    Swappy *swappy = reinterpret_cast<Swappy *>(data);
    if (!swappy) {
        ALOGE("Failed to get Swappy instance in onChoreographer");
        return;
    }
    swappy->handleChoreographer();
}

bool Swappy::swap(EGLDisplay display, EGLSurface surface) {
    TRACE_CALL();

    Swappy *swappy = getInstance();
    if (!swappy) {
        ALOGE("Failed to get Swappy instance in swap");
        return EGL_FALSE;
    }

    swappy->mChoreographerThread->postFrameCallbacks();
    swappy->waitForNextFrame(display);

    const auto swapStart = std::chrono::steady_clock::now();

    bool result = swappy->setPresentationTime(display, surface);
    if (!result) {
        return result;
    }

    swappy->resetSyncFence(display);

    result = (eglSwapBuffers(display, surface) == EGL_TRUE);

    swappy->updateSwapDuration(std::chrono::steady_clock::now() - swapStart);

    // This is the start of the next frame
    swappy->startFrame();

    return result;
}

Swappy *Swappy::getInstance() {
    std::lock_guard<std::mutex> lock(sInstanceMutex);
    return sInstance.get();
}

void Swappy::destroyInstance() {
    std::lock_guard<std::mutex> lock(sInstanceMutex);
    sInstance.reset();
}

EGL *Swappy::getEgl() {
    static thread_local EGL *egl = nullptr;
    if (!egl) {
        std::lock_guard lock(mEglMutex);
        egl = mEgl.get();
    }
    return egl;
}

Swappy::Swappy(nanoseconds refreshPeriod,
               nanoseconds appOffset,
               nanoseconds sfOffset,
               ConstructorTag /*tag*/)
    : mRefreshPeriod(refreshPeriod),
      mChoreographerFilter(std::make_unique<ChoreographerFilter>(refreshPeriod,
                                                                 sfOffset - appOffset,
                                                                 [this]() { return wakeClient(); })),
      mChoreographerThread(std::make_unique<ChoreographerThread>(std::bind(onChoreographer, this))) {

    Settings::getInstance()->addListener([this]() { onSettingsChanged(); });

    ALOGI("Initialized Swappy with refreshPeriod=%lld, appOffset=%lld, sfOffset=%lld",
          refreshPeriod.count(), appOffset.count(), sfOffset.count());
    std::lock_guard lock(mEglMutex);
    mEgl = EGL::create(refreshPeriod);
    if (!mEgl) {
        ALOGE("Failed to load EGL functions");
        exit(0);
    }
}

void Swappy::onSettingsChanged() {
    mSwapInterval = Settings::getInstance()->getSwapInterval();
}

void Swappy::handleChoreographer() {
    mChoreographerFilter->onChoreographer();
}

std::chrono::nanoseconds Swappy::wakeClient() {
    std::lock_guard<std::mutex> lock(mWaitingMutex);
    ++mCurrentFrame;

    // We're attempting to align with SurfaceFlinger's vsync, but it's always better to be a little
    // late than a little early (since a little early could cause our frame to be picked up
    // prematurely), so we pad by an additional millisecond.
    mCurrentFrameTimestamp = std::chrono::steady_clock::now() + mSwapDuration.load() + 1ms;
    mWaitingCondition.notify_all();
    return mSwapDuration;
}

void Swappy::startFrame() {
    TRACE_CALL();
    const auto [currentFrame, currentFrameTimestamp] = [this] {
        std::unique_lock<std::mutex> lock(mWaitingMutex);
        return std::make_tuple(mCurrentFrame, mCurrentFrameTimestamp);
    }();
    mTargetFrame = currentFrame + mSwapInterval;

    // We compute the target time as now
    //   + the presumed time generating the frame on the CPU (1 swap period)
    //   + the time the buffer will be on the GPU and in the queue to the compositor (1 swap period)
    mPresentationTime = currentFrameTimestamp + (mSwapInterval * 2) * mRefreshPeriod;
}

void Swappy::waitUntil(int32_t frameNumber) {
    TRACE_CALL();
    std::unique_lock<std::mutex> lock(mWaitingMutex);
    mWaitingCondition.wait(lock, [&]() { return mCurrentFrame >= frameNumber; });
}

void Swappy::waitOneFrame() {
    TRACE_CALL();
    std::unique_lock<std::mutex> lock(mWaitingMutex);
    const int32_t target = mCurrentFrame + 1;
    mWaitingCondition.wait(lock, [&]() { return mCurrentFrame >= target; });
}

void Swappy::waitForNextFrame(EGLDisplay display) {
    waitUntil(mTargetFrame);

    // If the frame hasn't completed yet, go into frame-by-frame slip until it completes
    while (!getEgl()->lastFrameIsComplete(display)) {
        ScopedTrace trace("lastFrameIncomplete");
        waitOneFrame();
    }
}

void Swappy::resetSyncFence(EGLDisplay display) {
    getEgl()->resetSyncFence(display);
}

bool Swappy::setPresentationTime(EGLDisplay display, EGLSurface surface) {
    TRACE_CALL();
    return getEgl()->setPresentationTime(display, surface, mPresentationTime);
}

void Swappy::updateSwapDuration(std::chrono::nanoseconds duration) {
    // TODO: The exponential smoothing factor here is arbitrary
    mSwapDuration = (mSwapDuration.load() * 4 / 5) + duration / 5;

    // Clamp the swap duration to half the refresh period
    //
    // We do this since the swap duration can be a bit noisy during periods such as app startup,
    // which can cause some stuttering as the smoothing catches up with the actual duration. By
    // clamping, we reduce the maximum error which reduces the calibration time.
    if (mSwapDuration.load() > (mRefreshPeriod / 2)) mSwapDuration = mRefreshPeriod / 2;
}
