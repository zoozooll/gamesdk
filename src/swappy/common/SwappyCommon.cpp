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

#include "SwappyCommon.h"

#include <cmath>
#include <thread>
#include <cstdlib>

#include "Settings.h"
#include "Thread.h"
#include "Log.h"
#include "Trace.h"

#define LOG_TAG "SwappyCommon"

namespace swappy {

using std::chrono::milliseconds;
using std::chrono::nanoseconds;

// NB These are only needed for C++14
constexpr std::chrono::nanoseconds SwappyCommon::FrameDuration::MAX_DURATION;
constexpr std::chrono::nanoseconds SwappyCommon::FRAME_HYSTERESIS;

SwappyCommon::SwappyCommon(JavaVM* vm,
                         std::chrono::nanoseconds refreshPeriod,
                         std::chrono::nanoseconds appOffset,
                         std::chrono::nanoseconds sfOffset)
        : mChoreographerFilter(std::make_unique<ChoreographerFilter>(refreshPeriod,
                                                                     sfOffset - appOffset,
                                                                     [this]() { return wakeClient(); })),
          mChoreographerThread(ChoreographerThread::createChoreographerThread(
                  ChoreographerThread::Type::Swappy,
                  vm,
                  [this]{ mChoreographerFilter->onChoreographer(); })),
          mSwapDuration(std::chrono::nanoseconds(0)),
          mRefreshPeriod(refreshPeriod),
          mSwapInterval(1),
          mAutoSwapInterval(1)
{
    Settings::getInstance()->addListener([this]() { onSettingsChanged(); });

    mAutoSwapIntervalThreshold = (1e9f / mRefreshPeriod.count()) / 20; // 20FPS
    mFrameDurations.reserve(mFrameDurationSamples);
}

SwappyCommon::~SwappyCommon() {
    // destroy all threads first before the other members of this class
    mChoreographerFilter.reset();
    mChoreographerThread.reset();

    Settings::reset();
}

std::chrono::nanoseconds SwappyCommon::wakeClient() {
    std::lock_guard<std::mutex> lock(mWaitingMutex);
    ++mCurrentFrame;

    // We're attempting to align with SurfaceFlinger's vsync, but it's always better to be a little
    // late than a little early (since a little early could cause our frame to be picked up
    // prematurely), so we pad by an additional millisecond.
    mCurrentFrameTimestamp = std::chrono::steady_clock::now() + mSwapDuration.load() + 1ms;
    mWaitingCondition.notify_all();
    return mSwapDuration;
}

void SwappyCommon::onChoreographer(int64_t frameTimeNanos) {
    TRACE_CALL();

    if (!mUsingExternalChoreographer) {
        mUsingExternalChoreographer = true;
        mChoreographerThread =
                ChoreographerThread::createChoreographerThread(
                        ChoreographerThread::Type::App,
                        nullptr,
                        [this] { mChoreographerFilter->onChoreographer(); });
    }

    mChoreographerThread->postFrameCallbacks();
}

bool SwappyCommon::waitForNextFrame(const SwapHandlers& h) {
    int lateFrames = 0;
    bool presentationTimeIsNeeded;

    const std::chrono::nanoseconds cpuTime = std::chrono::steady_clock::now() - mStartFrameTime;

    preWaitCallbacks();

    // if we are running slower than the threshold there is no point to sleep, just let the
    // app run as fast as it can
    if (mAutoSwapInterval <= mAutoSwapIntervalThreshold) {
        waitUntilTargetFrame();

        // wait for the previous frame to be rendered
        while (!h.lastFrameIsComplete()) {
            lateFrames++;
            waitOneFrame();
        }

        mPresentationTime += lateFrames * mRefreshPeriod;
        presentationTimeIsNeeded = true;
    } else {
        presentationTimeIsNeeded = false;
    }

    const std::chrono::nanoseconds gpuTime = h.getPrevFrameGpuTime();
    addFrameDuration({cpuTime, gpuTime});
    postWaitCallbacks();

    return presentationTimeIsNeeded;
}

void SwappyCommon::onPreSwap(const SwapHandlers& h) {
    if (!mUsingExternalChoreographer) {
        mChoreographerThread->postFrameCallbacks();
    }

    // for non pipeline mode where both cpu and gpu work is done at the same stage
    // wait for next frame will happen after swap
    if (mPipelineMode) {
        mPresentationTimeNeeded = waitForNextFrame(h);
    } else {
        mPresentationTimeNeeded = mAutoSwapInterval <= mAutoSwapIntervalThreshold;
    }

    mSwapTime = std::chrono::steady_clock::now();
    preSwapBuffersCallbacks();
}

void SwappyCommon::onPostSwap(const SwapHandlers& h) {
    postSwapBuffersCallbacks();

    if (updateSwapInterval()) {
        swapIntervalChangedCallbacks();
        TRACE_INT("mPipelineMode", mPipelineMode);
        TRACE_INT("mAutoSwapInterval", mAutoSwapInterval);
    }

    updateSwapDuration(std::chrono::steady_clock::now() - mSwapTime);

    if (!mPipelineMode) {
        waitForNextFrame(h);
    }

    startFrame();
}

void SwappyCommon::updateSwapDuration(std::chrono::nanoseconds duration) {
    // TODO: The exponential smoothing factor here is arbitrary
    mSwapDuration = (mSwapDuration.load() * 4 / 5) + duration / 5;

    // Clamp the swap duration to half the refresh period
    //
    // We do this since the swap duration can be a bit noisy during periods such as app startup,
    // which can cause some stuttering as the smoothing catches up with the actual duration. By
    // clamping, we reduce the maximum error which reduces the calibration time.
    if (mSwapDuration.load() > (mRefreshPeriod / 2)) mSwapDuration = mRefreshPeriod / 2;
}

uint64_t SwappyCommon::getSwapIntervalNS() {
    std::lock_guard<std::mutex> lock(mFrameDurationsMutex);
    return mAutoSwapInterval.load() * mRefreshPeriod.count();
};

void SwappyCommon::addFrameDuration(FrameDuration duration) {
    ALOGV("cpuTime = %.2f", duration.getCpuTime().count() / 1e6f);
    ALOGV("gpuTime = %.2f", duration.getGpuTime().count() / 1e6f);

    std::lock_guard<std::mutex> lock(mFrameDurationsMutex);
    // keep a sliding window of mFrameDurationSamples
    if (mFrameDurations.size() == mFrameDurationSamples) {
        mFrameDurationsSum -= mFrameDurations.front();
        mFrameDurations.erase(mFrameDurations.begin());
    }

    mFrameDurations.push_back(duration);
    mFrameDurationsSum += duration;
}

void SwappyCommon::swapSlower(const FrameDuration& averageFrameTime,
                        const std::chrono::nanoseconds& upperBound,
                        const std::chrono::nanoseconds& lowerBound,
                        const int32_t& newSwapInterval) {
    ALOGV("Rendering takes too much time for the given config");

    if (!mPipelineMode && averageFrameTime.getTime(true) <= upperBound) {
        ALOGV("turning on pipelining");
        mPipelineMode = true;
    } else {
        mAutoSwapInterval = newSwapInterval;
        ALOGV("Changing Swap interval to %d", mAutoSwapInterval.load());

        // since we changed the swap interval, we may be able to turn off pipeline mode
        nanoseconds newBound = mRefreshPeriod * mAutoSwapInterval.load();
        newBound -= (FRAME_HYSTERESIS * 2);
        if (mPipelineModeAutoMode && averageFrameTime.getTime(false) < newBound) {
            ALOGV("Turning off pipelining");
            mPipelineMode = false;
        } else {
            ALOGV("Turning on pipelining");
            mPipelineMode = true;
        }
    }
}

void SwappyCommon::swapFaster(const FrameDuration& averageFrameTime,
                        const std::chrono::nanoseconds& upperBound,
                        const std::chrono::nanoseconds& lowerBound,
                        const int32_t& newSwapInterval) {
    ALOGV("Rendering is much shorter for the given config");
    mAutoSwapInterval = newSwapInterval;
    ALOGV("Changing Swap interval to %d", mAutoSwapInterval.load());

    // since we changed the swap interval, we may need to turn on pipeline mode
    nanoseconds newBound = mRefreshPeriod * mAutoSwapInterval.load();
    newBound -= FRAME_HYSTERESIS;
    if (!mPipelineModeAutoMode || averageFrameTime.getTime(false) > newBound) {
        ALOGV("Turning on pipelining");
        mPipelineMode = true;
    } else {
        ALOGV("Turning off pipelining");
        mPipelineMode = false;
    }
}

bool SwappyCommon::updateSwapInterval() {
    std::lock_guard<std::mutex> lock(mFrameDurationsMutex);
    if (!mAutoSwapIntervalEnabled)
        return false;

    if (mFrameDurations.size() < mFrameDurationSamples)
        return false;

    const auto averageFrameTime = mFrameDurationsSum / mFrameDurations.size();
    // define lower and upper bound based on the swap duration
    nanoseconds upperBound = mRefreshPeriod * mAutoSwapInterval.load();
    nanoseconds lowerBound = mRefreshPeriod * (mAutoSwapInterval - 1);

    // to be on the conservative side, lower bounds by FRAME_HYSTERESIS
    upperBound -= FRAME_HYSTERESIS;
    lowerBound -= FRAME_HYSTERESIS;

    // add the hysteresis to one of the bounds to avoid going back and forth when frames
    // are exactly at the edge.
    lowerBound -= FRAME_HYSTERESIS;

    auto div_result = div((averageFrameTime.getTime(true) + FRAME_HYSTERESIS).count(),
                               mRefreshPeriod.count());
    auto framesPerRefresh = div_result.quot;
    auto framesPerRefreshRemainder = div_result.rem;

    const int32_t newSwapInterval = framesPerRefresh + (framesPerRefreshRemainder ? 1 : 0);

    ALOGV("mPipelineMode = %d", mPipelineMode);
    ALOGV("Average cpu frame time = %.2f", (averageFrameTime.getCpuTime().count()) / 1e6f);
    ALOGV("Average gpu frame time = %.2f", (averageFrameTime.getGpuTime().count()) / 1e6f);
    ALOGV("upperBound = %.2f", upperBound.count() / 1e6f);
    ALOGV("lowerBound = %.2f", lowerBound.count() / 1e6f);

    bool configChanged = false;
    if (averageFrameTime.getTime(mPipelineMode) > upperBound) {
        swapSlower(averageFrameTime, upperBound, lowerBound, newSwapInterval);
        configChanged = true;
    } else if (mSwapInterval < mAutoSwapInterval &&
               (averageFrameTime.getTime(true) < lowerBound)) {
        swapFaster(averageFrameTime, upperBound, lowerBound, newSwapInterval);
        configChanged = true;
    } else if (mPipelineModeAutoMode && mPipelineMode &&
               averageFrameTime.getTime(false) < upperBound - FRAME_HYSTERESIS) {
        ALOGV("Rendering time fits the current swap interval without pipelining");
        mPipelineMode = false;
        configChanged = true;
    }

    if (configChanged) {
        mFrameDurationsSum = {};
        mFrameDurations.clear();
    }
    return configChanged;
}

template<typename Tracers, typename Func> void addToTracers(Tracers& tracers, Func func, void *userData) {
    if (func != nullptr) {
        tracers.push_back([func, userData](auto... params) {
            func(userData, params...);
        });
    }
}

void SwappyCommon::addTracerCallbacks(SwappyTracer tracer) {
    addToTracers(mInjectedTracers.preWait, tracer.preWait, tracer.userData);
    addToTracers(mInjectedTracers.postWait, tracer.postWait, tracer.userData);
    addToTracers(mInjectedTracers.preSwapBuffers, tracer.preSwapBuffers, tracer.userData);
    addToTracers(mInjectedTracers.postSwapBuffers, tracer.postSwapBuffers, tracer.userData);
    addToTracers(mInjectedTracers.startFrame, tracer.startFrame, tracer.userData);
    addToTracers(mInjectedTracers.swapIntervalChanged, tracer.swapIntervalChanged, tracer.userData);
}

template<typename T, typename ...Args> void executeTracers(T& tracers, Args... args) {
    for (const auto& tracer : tracers) {
        tracer(std::forward<Args>(args)...);
    }
}

void SwappyCommon::preSwapBuffersCallbacks() {
    executeTracers(mInjectedTracers.preSwapBuffers);
}

void SwappyCommon::postSwapBuffersCallbacks() {
    executeTracers(mInjectedTracers.postSwapBuffers,
                   (long) mPresentationTime.time_since_epoch().count());
}

void SwappyCommon::preWaitCallbacks() {
    executeTracers(mInjectedTracers.preWait);
}

void SwappyCommon::postWaitCallbacks() {
    executeTracers(mInjectedTracers.postWait);
}

void SwappyCommon::startFrameCallbacks() {
    executeTracers(mInjectedTracers.startFrame,
                   mCurrentFrame,
                   (long) mCurrentFrameTimestamp.time_since_epoch().count());
}

void SwappyCommon::swapIntervalChangedCallbacks() {
    executeTracers(mInjectedTracers.swapIntervalChanged);
}

void SwappyCommon::setAutoSwapInterval(bool enabled) {
    std::lock_guard<std::mutex> lock(mFrameDurationsMutex);
    mAutoSwapIntervalEnabled = enabled;

    // non pipeline mode is not supported when auto mode is disabled
    if (!enabled) {
        mPipelineMode = true;
        TRACE_INT("mPipelineMode", mPipelineMode);
    }
}

void SwappyCommon::setAutoPipelineMode(bool enabled) {
    std::lock_guard<std::mutex> lock(mFrameDurationsMutex);
    mPipelineModeAutoMode = enabled;
    TRACE_INT("mPipelineModeAutoMode", mPipelineModeAutoMode);
    if (!enabled) {
        mPipelineMode = true;
        TRACE_INT("mPipelineMode", mPipelineMode);
    }
}

void SwappyCommon::onSettingsChanged() {
    std::lock_guard<std::mutex> lock(mFrameDurationsMutex);
    int32_t newSwapInterval = round(float(Settings::getInstance()->getSwapIntervalNS()) /
                                         float(mRefreshPeriod.count()));
    if (mSwapInterval != newSwapInterval || mAutoSwapInterval != newSwapInterval) {
        mSwapInterval = newSwapInterval;
        mAutoSwapInterval = mSwapInterval.load();
        mFrameDurations.clear();
        mFrameDurationsSum = {};
    }
    TRACE_INT("mSwapInterval", mSwapInterval);
    TRACE_INT("mAutoSwapInterval", mAutoSwapInterval);
}

void SwappyCommon::startFrame() {
    TRACE_CALL();

    int32_t currentFrame;
    std::chrono::steady_clock::time_point currentFrameTimestamp;
    {
        std::unique_lock<std::mutex> lock(mWaitingMutex);
        currentFrame = mCurrentFrame;
        currentFrameTimestamp = mCurrentFrameTimestamp;
    }

    startFrameCallbacks();

    mTargetFrame = currentFrame + mAutoSwapInterval;

    const int intervals = (mPipelineMode) ? 2 : 1;

    // We compute the target time as now
    //   + the time the buffer will be on the GPU and in the queue to the compositor (1 swap period)
    mPresentationTime = currentFrameTimestamp + (mAutoSwapInterval * intervals) * mRefreshPeriod;

    mStartFrameTime = std::chrono::steady_clock::now();
}

void SwappyCommon::waitUntilTargetFrame() {
    TRACE_CALL();
    std::unique_lock<std::mutex> lock(mWaitingMutex);
    mWaitingCondition.wait(lock, [&]() { return mCurrentFrame >= mTargetFrame; });
}

void SwappyCommon::waitOneFrame() {
    TRACE_CALL();
    std::unique_lock<std::mutex> lock(mWaitingMutex);
    const int32_t target = mCurrentFrame + 1;
    mWaitingCondition.wait(lock, [&]() { return mCurrentFrame >= target; });
}


} // namespace swappy
