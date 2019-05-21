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

#pragma once

#include <jni.h>
#include <chrono>
#include <mutex>

#include "swappy/swappyGL.h"
#include "swappy/swappyGL_extra.h"

#include "SwappyCommon.h"
#include "EGL.h"
#include "FrameStatistics.h"

namespace swappy {

using EGLDisplay = void *;
using EGLSurface = void *;

using namespace std::chrono_literals;

class SwappyGL {
  private:
    // Allows construction with std::unique_ptr from a static method, but disallows construction
    // outside of the class since no one else can construct a ConstructorTag
    struct ConstructorTag {};
  public:
    SwappyGL(JavaVM *vm,
             std::chrono::nanoseconds refreshPeriod,
             std::chrono::nanoseconds appOffset,
             std::chrono::nanoseconds sfOffset,
             ConstructorTag);

    static void init(JNIEnv *env, jobject jactivity);

    static void onChoreographer(int64_t frameTimeNanos);

    static bool swap(EGLDisplay display, EGLSurface surface);

    // Pass callbacks for tracing within the swap function
    static void addTracer(const SwappyTracer *tracer);

    static uint64_t getSwapIntervalNS();

    static void setAutoSwapInterval(bool enabled);

    static void setAutoPipelineMode(bool enabled);

    static void enableStats(bool enabled);
    static void recordFrameStart(EGLDisplay display, EGLSurface surface);
    static void getStats(SwappyStats *stats);
    static bool isEnabled();
    static void destroyInstance();

private:
    static void init(JavaVM *vm,
                     std::chrono::nanoseconds refreshPeriod,
                     std::chrono::nanoseconds appOffset,
                     std::chrono::nanoseconds sfOffset);

    static SwappyGL *getInstance();

    bool enabled() const { return !mDisableSwappy; }

    EGL *getEgl();

    bool swapInternal(EGLDisplay display, EGLSurface surface);

    bool lastFrameIsComplete(EGLDisplay display);

    // Destroys the previous sync fence (if any) and creates a new one for this frame
    void resetSyncFence(EGLDisplay display);

    // Computes the desired presentation time based on the swap interval and sets it
    // using eglPresentationTimeANDROID
    bool setPresentationTime(EGLDisplay display, EGLSurface surface);

    bool mDisableSwappy = false;

    static std::mutex sInstanceMutex;
    static std::unique_ptr<SwappyGL> sInstance;

    std::mutex mEglMutex;
    std::shared_ptr<EGL> mEgl;

    std::unique_ptr<FrameStatistics> mFrameStatistics;

    const std::chrono::nanoseconds mSfOffset;

    SwappyCommon mCommonBase;
};

} //namespace swappy
