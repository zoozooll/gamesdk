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

#include <cmath>
#include <thread>

#include "Settings.h"
#include "Thread.h"
#include "ChoreographerFilter.h"
#include "ChoreographerThread.h"
#include "EGL.h"

#include "Log.h"
#include "Trace.h"

namespace swappy {

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
    JavaVM *vm;
    env->GetJavaVM(&vm);
    Swappy::init(
            vm,
            nanoseconds(vsyncPeriodNanos),
            nanoseconds(appVsyncOffsetNanos),
            nanoseconds(sfVsyncOffsetNanos));
}

void Swappy::init(JavaVM *vm, nanoseconds refreshPeriod, nanoseconds appOffset, nanoseconds sfOffset) {
    std::lock_guard<std::mutex> lock(sInstanceMutex);
    if (sInstance) {
        ALOGE("Attempted to initialize Swappy twice");
        return;
    }
    sInstance = std::make_unique<Swappy>(vm, refreshPeriod, appOffset, sfOffset, ConstructorTag{});
}

void Swappy::onChoreographer(int64_t frameTimeNanos) {
    TRACE_CALL();

    Swappy *swappy = getInstance();
    if (!swappy) {
        ALOGE("Failed to get Swappy instance in swap");
        return;
    }

    if (!swappy->mUsingExternalChoreographer) {
        swappy->mUsingExternalChoreographer = true;
        swappy->mChoreographerThread =
                ChoreographerThread::createChoreographerThread(
                        ChoreographerThread::Type::App,
                        nullptr,
                        [swappy] { swappy->handleChoreographer(); });
    }

    swappy->mChoreographerThread->postFrameCallbacks();
}

bool Swappy::swap(EGLDisplay display, EGLSurface surface) {
    TRACE_CALL();

    Swappy *swappy = getInstance();
    if (!swappy) {
        ALOGE("Failed to get Swappy instance in swap");
        return EGL_FALSE;
    }

    return swappy->swapInternal(display, surface);
}

bool Swappy::swapInternal(EGLDisplay display, EGLSurface surface) {
    if (!mUsingExternalChoreographer) {
        mChoreographerThread->postFrameCallbacks();
    }

    waitForNextFrame(display);

    mSwapTime = std::chrono::steady_clock::now();

    bool result = setPresentationTime(display, surface);
    if (!result) {
        return result;
    }

    resetSyncFence(display);

    preSwapBuffersCallbacks();

    result = (eglSwapBuffers(display, surface) == EGL_TRUE);

    postSwapBuffersCallbacks();

    if (updateSwapInterval()) {
        swapIntervalChangedCallbacks();
    }

    updateSwapDuration(std::chrono::steady_clock::now() - mSwapTime);

    startFrame();

    return result;
}

void Swappy::addTracer(const SwappyTracer *tracer) {
    Swappy *swappy = getInstance();
    if (!swappy) {
        ALOGE("Failed to get Swappy instance in addTracer");
        return;
    }
    swappy->addTracerCallbacks(*tracer);
}

uint64_t Swappy::getSwapIntervalNS() {
    Swappy *swappy = getInstance();
    if (!swappy) {
        ALOGE("Failed to get Swappy instance in getSwapIntervalNS");
        return -1;
    }

    std::lock_guard lock(swappy->mFrameDurationsMutex);
    return swappy->mAutoSwapInterval.load() * swappy->mRefreshPeriod.count();
};

void Swappy::setAutoSwapInterval(bool enabled) {
    Swappy *swappy = getInstance();
    if (!swappy) {
        ALOGE("Failed to get Swappy instance in setAutoSwapInterval");
        return;
    }

    std::lock_guard<std::mutex> lock(swappy->mFrameDurationsMutex);
    swappy->mAutoSwapIntervalEnabled = enabled;
}

Swappy *Swappy::getInstance() {
    std::lock_guard<std::mutex> lock(sInstanceMutex);
    return sInstance.get();
}

void Swappy::destroyInstance() {
    std::lock_guard<std::mutex> lock(sInstanceMutex);
    sInstance.reset();
}

template<typename Tracers, typename Func> void addToTracers(Tracers& tracers, Func func, void *userData) {
    if (func != nullptr) {
        tracers.push_back(std::bind(func, userData));
    }
}

void Swappy::addTracerCallbacks(SwappyTracer tracer) {
    addToTracers(mInjectedTracers.preWait, tracer.preWait, tracer.userData);
    addToTracers(mInjectedTracers.postWait, tracer.postWait, tracer.userData);
    addToTracers(mInjectedTracers.preSwapBuffers, tracer.preSwapBuffers, tracer.userData);
    addToTracers(mInjectedTracers.postSwapBuffers, tracer.postSwapBuffers, tracer.userData);
    addToTracers(mInjectedTracers.startFrame, tracer.startFrame, tracer.userData);
    addToTracers(mInjectedTracers.swapIntervalChanged, tracer.swapIntervalChanged, tracer.userData);
}

template<typename T> void executeTracers(T& tracers) {
    for (const auto& tracer : tracers) {
        tracer();
    }
}

void Swappy::preSwapBuffersCallbacks() {
    executeTracers(mInjectedTracers.preSwapBuffers);
}

void Swappy::postSwapBuffersCallbacks() {
    executeTracers(mInjectedTracers.postSwapBuffers);
}

void Swappy::preWaitCallbacks() {
    executeTracers(mInjectedTracers.preWait);
}

void Swappy::postWaitCallbacks() {
    executeTracers(mInjectedTracers.postWait);
}

void Swappy::startFrameCallbacks() {
    executeTracers(mInjectedTracers.startFrame);
}

void Swappy::swapIntervalChangedCallbacks() {
    executeTracers(mInjectedTracers.swapIntervalChanged);
}

EGL *Swappy::getEgl() {
    static thread_local EGL *egl = nullptr;
    if (!egl) {
        std::lock_guard lock(mEglMutex);
        egl = mEgl.get();
    }
    return egl;
}

Swappy::Swappy(JavaVM *vm,
               nanoseconds refreshPeriod,
               nanoseconds appOffset,
               nanoseconds sfOffset,
               ConstructorTag /*tag*/)
    : mRefreshPeriod(refreshPeriod),
      mChoreographerFilter(std::make_unique<ChoreographerFilter>(refreshPeriod,
                                                                 sfOffset - appOffset,
                                                                 [this]() { return wakeClient(); })),
      mChoreographerThread(ChoreographerThread::createChoreographerThread(
              ChoreographerThread::Type::Swappy,
              vm,
              [this]{ handleChoreographer(); }))
{

    Settings::getInstance()->addListener([this]() { onSettingsChanged(); });

    ALOGI("Initialized Swappy with refreshPeriod=%lld, appOffset=%lld, sfOffset=%lld",
          refreshPeriod.count(), appOffset.count(), sfOffset.count());
    std::lock_guard lock(mEglMutex);
    mEgl = EGL::create(refreshPeriod);
    if (!mEgl) {
        ALOGE("Failed to load EGL functions");
        exit(0);
    }

    mAutoSwapIntervalThreshold = (1e9f / mRefreshPeriod.count()) / 20; // 20FPS
    mFrameDurationSamples = 1e9f / mRefreshPeriod.count(); // 1 second
    mFrameDurations.reserve(mFrameDurationSamples);
}

void Swappy::onSettingsChanged() {
    std::lock_guard<std::mutex> lock(mFrameDurationsMutex);
    int32_t newSwapInterval = std::round(float(Settings::getInstance()->getSwapIntervalNS()) /
                               float(mRefreshPeriod.count()));
    if (mSwapInterval != newSwapInterval || mAutoSwapInterval != newSwapInterval) {
        mSwapInterval = newSwapInterval;
        mAutoSwapInterval = mSwapInterval.load();
        mFrameDurations.clear();
        mFrameDurationsSum = 0;
    }
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

    startFrameCallbacks();

    const auto [currentFrame, currentFrameTimestamp] = [this] {
        std::unique_lock<std::mutex> lock(mWaitingMutex);
        return std::make_tuple(mCurrentFrame, mCurrentFrameTimestamp);
    }();
    mTargetFrame = currentFrame + mAutoSwapInterval;

    // We compute the target time as now
    //   + the presumed time generating the frame on the CPU (1 swap period)
    //   + the time the buffer will be on the GPU and in the queue to the compositor (1 swap period)
    mPresentationTime = currentFrameTimestamp + (mAutoSwapInterval * 2) * mRefreshPeriod;
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

void Swappy::recordFrameTime(int frames) {
    std::lock_guard<std::mutex> lock(mFrameDurationsMutex);

    // keep a sliding window of mFrameDurationSamples
    if (mFrameDurations.size() == mFrameDurationSamples) {
        mFrameDurationsSum -= mFrameDurations.front();
        mFrameDurations.erase(mFrameDurations.begin());
    }

    mFrameDurations.push_back(frames);
    mFrameDurationsSum += frames;
}

int32_t Swappy::nanoToSwapInterval(std::chrono::nanoseconds nano) {
    int32_t interval = nano / mRefreshPeriod;

    // round the number based on the nearest
    if (nano.count() - (interval * mRefreshPeriod.count()) > mRefreshPeriod.count() / 2) {
        return interval + 1;
    } else {
        return interval;
    }
}

void Swappy::waitForNextFrame(EGLDisplay display) {
    preWaitCallbacks();

    int lateFrames = 0;

    // if we are running slower than the threshold there is no point to sleep, just let the
    // app run as fast as it can
    if (mAutoSwapInterval <= mAutoSwapIntervalThreshold) {
        // while we wait for the target frame,
        // the previous frame might have finished rendering early
        while (mCurrentFrame < mTargetFrame) {
            if (getEgl()->lastFrameIsComplete(display)) {
                lateFrames--;
            }
            waitOneFrame();
        }

        // we reached the target frame, now we wait for rendering to be done for late frames
        while (!getEgl()->lastFrameIsComplete(display)) {
            gamesdk::ScopedTrace trace("lastFrameIncomplete");
            waitOneFrame();
        }

        // adjust presentation time if needed
        int32_t frameDiff = mCurrentFrame - mTargetFrame;
        frameDiff = (frameDiff / mAutoSwapInterval.load());
        mPresentationTime += frameDiff * mRefreshPeriod;

        lateFrames += frameDiff;
        recordFrameTime(mAutoSwapInterval + lateFrames);
    } else {
        auto timeNow = std::chrono::steady_clock::now();
        mPresentationTime = timeNow;
        recordFrameTime(nanoToSwapInterval(timeNow - mSwapTime));
    }

    postWaitCallbacks();
}

bool Swappy::updateSwapInterval() {
    std::lock_guard<std::mutex> lock(mFrameDurationsMutex);

    if (!mAutoSwapIntervalEnabled)
        return false;

    if (mFrameDurations.size() < mFrameDurationSamples)
        return false;

    float averageFrameTime = float(mFrameDurationsSum) / mFrameDurations.size();

    // apply hysteresis when checking the average to avoid going back and forth when frames
    // are exactly at the edge
    if (averageFrameTime > mAutoSwapInterval * (1 + FRAME_AVERAGE_HYSTERESIS)) {
        mAutoSwapInterval++;
        return true;
    }

    if (mSwapInterval < mAutoSwapInterval &&
        averageFrameTime < (mAutoSwapInterval - 1) * (1 - FRAME_AVERAGE_HYSTERESIS)) {
        mAutoSwapInterval--;
        return true;
    }

    return false;
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

} // namespace swappy
