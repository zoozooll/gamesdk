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

#include <chrono>
#include <memory>
#include <mutex>
#include <list>
#include <vector>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <jni.h>

#include "swappy/swappy.h"
#include "swappy/swappy_extra.h"

#include "Thread.h"

namespace swappy {

class ChoreographerFilter;
class ChoreographerThread;
class EGL;
class FrameStatistics;

using EGLDisplay = void *;
using EGLSurface = void *;

class Swappy {
  private:
    // Allows construction with std::unique_ptr from a static method, but disallows construction
    // outside of the class since no one else can construct a ConstructorTag
    struct ConstructorTag {
    };
  public:
    Swappy(JavaVM *vm,
           std::chrono::nanoseconds refreshPeriod,
           std::chrono::nanoseconds appOffset,
           std::chrono::nanoseconds sfOffset,
           ConstructorTag tag);

    static void init(JNIEnv *env, jobject jactivity);

    static void onChoreographer(int64_t frameTimeNanos);

    static bool swap(EGLDisplay display, EGLSurface surface);

    static void init(JavaVM *vm,
                     std::chrono::nanoseconds refreshPeriod,
                     std::chrono::nanoseconds appOffset,
                     std::chrono::nanoseconds sfOffset);

    // Pass callbacks for tracing within the swap function
    static void addTracer(const SwappyTracer *tracer);

    static uint64_t getSwapIntervalNS();

    static void setAutoSwapInterval(bool enabled);

    static void overrideAutoSwapInterval(uint64_t swap_ns);

    static void enableStats(bool enabled);
    static void recordFrameStart(EGLDisplay display, EGLSurface surface);
    static void getStats(Swappy_Stats *stats);
    static void destroyInstance();

private:
    static Swappy *getInstance();

    EGL *getEgl();

    bool swapInternal(EGLDisplay display, EGLSurface surface);

    void addTracerCallbacks(SwappyTracer tracer);

    void preSwapBuffersCallbacks();
    void postSwapBuffersCallbacks();
    void preWaitCallbacks();
    void postWaitCallbacks();
    void startFrameCallbacks();
    void swapIntervalChangedCallbacks();

    void onSettingsChanged();

    void handleChoreographer();
    std::chrono::nanoseconds wakeClient();

    void startFrame();

    void waitUntil(int32_t frameNumber);

    void waitOneFrame();

    // Waits for the next frame, considering both Choreographer and the prior frame's completion
    bool waitForNextFrame(EGLDisplay display);

    // Destroys the previous sync fence (if any) and creates a new one for this frame
    void resetSyncFence(EGLDisplay display);

    // Computes the desired presentation time based on the swap interval and sets it
    // using eglPresentationTimeANDROID
    bool setPresentationTime(EGLDisplay display, EGLSurface surface);

    void updateSwapDuration(std::chrono::nanoseconds duration);

    void recordFrameTime(int frames);

    bool updateSwapInterval();

    int32_t nanoToSwapInterval(std::chrono::nanoseconds);

    std::atomic<std::chrono::nanoseconds> mSwapDuration = std::chrono::nanoseconds(0);

    static std::mutex sInstanceMutex;
    static std::unique_ptr<Swappy> sInstance;

    std::atomic<int32_t> mSwapInterval = 0;
    std::atomic<int32_t> mAutoSwapInterval = 0;
    int mAutoSwapIntervalThreshold = 0;

    std::mutex mWaitingMutex;
    std::condition_variable mWaitingCondition;
    std::chrono::steady_clock::time_point mCurrentFrameTimestamp = std::chrono::steady_clock::now();
    int32_t mCurrentFrame = 0;

    std::mutex mEglMutex;
    std::shared_ptr<EGL> mEgl;

    int32_t mTargetFrame = 0;
    std::chrono::steady_clock::time_point mPresentationTime = std::chrono::steady_clock::now();

    const std::chrono::nanoseconds mRefreshPeriod;
    std::unique_ptr<ChoreographerFilter> mChoreographerFilter;

    bool mUsingExternalChoreographer = false;
    std::unique_ptr<ChoreographerThread> mChoreographerThread;

    template <typename ...T>
    using Tracer = std::function<void (T...)>;

    struct SwappyTracerCallbacks {
        std::list<Tracer<>> preWait;
        std::list<Tracer<>> postWait;
        std::list<Tracer<>> preSwapBuffers;
        std::list<Tracer<long>> postSwapBuffers;
        std::list<Tracer<int32_t, long>> startFrame;
        std::list<Tracer<>> swapIntervalChanged;
    };

    SwappyTracerCallbacks mInjectedTracers;

    std::mutex mFrameDurationsMutex;
    std::vector<int> mFrameDurations GUARDED_BY(mFrameDurationsMutex);
    int mFrameDurationsSum GUARDED_BY(mFrameDurationsMutex) = 0;
    int mFrameDurationSamples GUARDED_BY(mFrameDurationsMutex);
    bool mAutoSwapIntervalEnabled GUARDED_BY(mFrameDurationsMutex) = true;
    static constexpr float FRAME_AVERAGE_HYSTERESIS = 0.1;
    std::chrono::steady_clock::time_point mSwapTime;
    std::unique_ptr<FrameStatistics> mFrameStatistics;
};

} //namespace swappy
