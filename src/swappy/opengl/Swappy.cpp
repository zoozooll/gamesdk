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

#include <cmath>
#include <thread>
#include <cstdlib>
#include <cinttypes>

#include "Thread.h"
#include "SystemProperties.h"

#define LOG_TAG "Swappy"

#include "Log.h"
#include "Trace.h"

namespace swappy {

using std::chrono::milliseconds;
using std::chrono::nanoseconds;

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

    // getAppVsyncOffsetNanos was only added in API 21.
    // Return gracefully if this device doesn't support it.
    if (getAppVsyncOffsetNanos == 0 || env->ExceptionOccurred()) {
        env->ExceptionClear();
        return;
    }
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

    swappy->mCommonBase.onChoreographer(frameTimeNanos);
}

bool Swappy::swap(EGLDisplay display, EGLSurface surface) {
    TRACE_CALL();

    Swappy *swappy = getInstance();
    if (!swappy) {
        ALOGE("Failed to get Swappy instance in swap");
        return EGL_FALSE;
    }

    if (swappy->enabled()) {
        return swappy->swapInternal(display, surface);
    } else {
        return eglSwapBuffers(display, surface) == EGL_TRUE;
    }
}



bool Swappy::lastFrameIsComplete(EGLDisplay display) {
    if (!getEgl()->lastFrameIsComplete(display)) {
        gamesdk::ScopedTrace trace("lastFrameIncomplete");
        ALOGV("lastFrameIncomplete");
        return false;
    }
    return true;
}

bool Swappy::swapInternal(EGLDisplay display, EGLSurface surface) {
    const SwappyCommon::SwapHandlers handlers = {
            .lastFrameIsComplete = [&]() { return lastFrameIsComplete(display); },
            .getPrevFrameGpuTime = [&]() { return getEgl()->getFencePendingTime(); },
    };

    mCommonBase.onPreSwap(handlers);

    if (mCommonBase.needToSetPresentationTime()) {
        bool setPresentationTimeResult = setPresentationTime(display, surface);
        if (!setPresentationTimeResult) {
            return setPresentationTimeResult;
        }
    }

    resetSyncFence(display);

    bool swapBuffersResult = (eglSwapBuffers(display, surface) == EGL_TRUE);

    mCommonBase.onPostSwap(handlers);

    return swapBuffersResult;
}

void Swappy::addTracer(const SwappyTracer *tracer) {
    Swappy *swappy = getInstance();
    if (!swappy) {
        ALOGE("Failed to get Swappy instance in addTracer");
        return;
    }
    swappy->mCommonBase.addTracerCallbacks(*tracer);
}

uint64_t Swappy::getSwapIntervalNS() {
    Swappy *swappy = getInstance();
    if (!swappy) {
        ALOGE("Failed to get Swappy instance in getSwapIntervalNS");
        return -1;
    }
    return swappy->mCommonBase.getSwapIntervalNS();
};

void Swappy::setAutoSwapInterval(bool enabled) {
    Swappy *swappy = getInstance();
    if (!swappy) {
        ALOGE("Failed to get Swappy instance in setAutoSwapInterval");
        return;
    }
    swappy->mCommonBase.setAutoSwapInterval(enabled);
}

void Swappy::setAutoPipelineMode(bool enabled) {
    Swappy *swappy = getInstance();
    if (!swappy) {
        ALOGE("Failed to get Swappy instance in setAutoPipelineMode");
        return;
    }
    swappy->mCommonBase.setAutoPipelineMode(enabled);
}

void Swappy::enableStats(bool enabled) {
    Swappy *swappy = getInstance();
    if (!swappy) {
        ALOGE("Failed to get Swappy instance in enableStats");
            return;
    }

    if (!swappy->enabled()) {
        return;
    }

    if (!swappy->getEgl()->statsSupported()) {
        ALOGI("stats are not suppored on this platform");
        return;
    }

    if (enabled && swappy->mFrameStatistics == nullptr) {
        swappy->mFrameStatistics = std::make_unique<FrameStatistics>(
                swappy->mEgl, swappy->mCommonBase.getRefreshPeriod());
        ALOGI("Enabling stats");
    } else {
        swappy->mFrameStatistics = nullptr;
        ALOGI("Disabling stats");
    }
}

void Swappy::recordFrameStart(EGLDisplay display, EGLSurface surface) {
    TRACE_CALL();
    Swappy *swappy = getInstance();
    if (!swappy) {
        ALOGE("Failed to get Swappy instance in recordFrameStart");
        return;
    }

    if (swappy->mFrameStatistics)
        swappy->mFrameStatistics->capture(display, surface);
}

void Swappy::getStats(Swappy_Stats *stats) {
    Swappy *swappy = getInstance();
    if (!swappy) {
        ALOGE("Failed to get Swappy instance in getStats");
        return;
    }

    if (swappy->mFrameStatistics)
        *stats = swappy->mFrameStatistics->getStats();
}

Swappy *Swappy::getInstance() {
    std::lock_guard<std::mutex> lock(sInstanceMutex);
    return sInstance.get();
}

bool Swappy::isEnabled() {
    Swappy *swappy = getInstance();
    if (!swappy) {
        ALOGE("Failed to get Swappy instance in getStats");
        return false;
    }
    return swappy->enabled();
}

void Swappy::destroyInstance() {
    std::lock_guard<std::mutex> lock(sInstanceMutex);
    sInstance.reset();
}

EGL *Swappy::getEgl() {
    static thread_local EGL *egl = nullptr;
    if (!egl) {
        std::lock_guard<std::mutex> lock(mEglMutex);
        egl = mEgl.get();
    }
    return egl;
}

Swappy::Swappy(JavaVM *vm,
               nanoseconds refreshPeriod,
               nanoseconds appOffset,
               nanoseconds sfOffset,
               ConstructorTag)
    : mFrameStatistics(nullptr),
      mSfOffset(sfOffset),
      mCommonBase(vm, refreshPeriod, appOffset, sfOffset)
{
    mDisableSwappy = getSystemPropViaGetAsBool("swappy.disable", false);
    if (!enabled()) {
        ALOGI("Swappy is disabled");
        return;
    }

    std::lock_guard<std::mutex> lock(mEglMutex);
    mEgl = EGL::create(refreshPeriod);
    if (!mEgl) {
        ALOGE("Failed to load EGL functions");
        mDisableSwappy = true;
        return;
    }

    ALOGI("Initialized Swappy with refreshPeriod=%lld, appOffset=%lld, sfOffset=%lld" ,
          (long long)refreshPeriod.count(), (long long)appOffset.count(),
          (long long)sfOffset.count());
}

void Swappy::resetSyncFence(EGLDisplay display) {
    getEgl()->resetSyncFence(display);
}

bool Swappy::setPresentationTime(EGLDisplay display, EGLSurface surface) {
    TRACE_CALL();

    // if we are too close to the vsync, there is no need to set presentation time
    if ((mCommonBase.getPresentationTime() - std::chrono::steady_clock::now()) <
            (mCommonBase.getRefreshPeriod() - mSfOffset)) {
        return EGL_TRUE;
    }

    return getEgl()->setPresentationTime(display, surface, mCommonBase.getPresentationTime());
}

} // namespace swappy
