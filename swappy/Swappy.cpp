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

#include "swappy-utils/Log.h"
#include "swappy-utils/Settings.h"
#include "swappy-utils/Trace.h"

#include "ChoreographerFilter.h"
#include "EGL.h"

using std::chrono::milliseconds;
using std::chrono::nanoseconds;
using namespace std::chrono_literals;

std::mutex Swappy::sInstanceMutex;
std::unique_ptr<Swappy> Swappy::sInstance;

void Swappy::init(nanoseconds refreshPeriod, nanoseconds appOffset, nanoseconds sfOffset) {
    std::lock_guard<std::mutex> lock(sInstanceMutex);
    if (sInstance) {
        ALOGE("Attempted to initialize Swappy twice");
        return;
    }
    sInstance = std::make_unique<Swappy>(refreshPeriod, appOffset, sfOffset, ConstructorTag{});
}

void Swappy::onChoreographer(int64_t /*frameTimeNanos*/) {
    Swappy *swappy = getInstance();
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

    swappy->waitForNextFrame(display);

    const bool result = swappy->setPresentationTime(display, surface);
    if (!result) {
        return result;
    }

    swappy->resetSyncFence(display);

    return eglSwapBuffers(display, surface) == EGL_TRUE;
}

void Swappy::sleepModulo(int32_t modulo) {
    Swappy *swappy = getInstance();
    if (!swappy) {
        ALOGE("Failed to get Swappy instance in sleepUntilNextFrame");
        return;
    }

    swappy->waitModulo(modulo);
}

Swappy *Swappy::getInstance() {
    std::lock_guard<std::mutex> lock(sInstanceMutex);
    return sInstance.get();
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
                                                                 [this]() { wakeClient(); })) {
    Settings::getInstance()->addListener([this]() { onSettingsChanged(); });

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

void Swappy::wakeClient() {
    std::lock_guard<std::mutex> lock(mWaitingMutex);
    ++mCurrentFrame;
    mWaitingCondition.notify_all();
}

int32_t Swappy::waitModulo(int32_t modulo) {
    TRACE_CALL();
    std::unique_lock<std::mutex> lock(mWaitingMutex);
    const int32_t target = mCurrentFrame + (mSwapInterval - mCurrentFrame % mSwapInterval) + modulo;
    mWaitingCondition.wait(lock, [&]() { return mCurrentFrame >= target; });
    return mCurrentFrame;
}

void Swappy::waitOneFrame() {
    TRACE_CALL();
    std::unique_lock<std::mutex> lock(mWaitingMutex);
    const int32_t target = mCurrentFrame + 1;
    mWaitingCondition.wait(lock, [&]() { return mCurrentFrame >= target; });
}

void Swappy::waitForNextFrame(EGLDisplay display) {
    waitModulo(0);

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
    return getEgl()->setPresentationTime(display, surface, mSwapInterval.load());
}