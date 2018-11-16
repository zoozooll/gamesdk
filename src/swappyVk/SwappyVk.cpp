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

#include "SwappyVk.h"
#include <map>
#include <unistd.h>

#include <dlfcn.h>
#include <cstdlib>

#ifdef ANDROID
#include <mutex>
#include <pthread.h>
#include <android/looper.h>
#include <android/choreographer.h>
#include <android/log.h>
#include <android/trace.h>

#define ATRACE_NAME(name) ScopedTrace ___tracer(name)

// ATRACE_CALL is an ATRACE_NAME that uses the current function name.
#define ATRACE_CALL() ATRACE_NAME(__FUNCTION__)

class ScopedTrace {
public:
    inline ScopedTrace(const char *name) {
        ATrace_beginSection(name);
    }

    inline ~ScopedTrace() {
        ATrace_endSection();
    }
};


#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, "SwappyVk", __VA_ARGS__)
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN, "SwappyVk", __VA_ARGS__)
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, "SwappyVk", __VA_ARGS__)
#define ALOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "SwappyVk", __VA_ARGS__)
#define ALOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, "SwappyVk", __VA_ARGS__)
#else
#define ATRACE_CALL() ((void)0)
#define ALOGE(...)    ((void)0)
#define ALOGW(...)    ((void)0)
#define ALOGD(...)    ((void)0)
#define ALOGV(...)    ((void)0)
#endif


constexpr uint32_t kThousand = 1000;
constexpr uint32_t kMillion  = 1000000;
constexpr uint32_t kBillion  = 1000000000;
constexpr uint32_t k16_6msec = 16666666;

constexpr uint32_t kTooCloseToVsyncBoundary     = 3000000;
constexpr uint32_t kTooFarAwayFromVsyncBoundary = 7000000;
constexpr uint32_t kNudgeWithinVsyncBoundaries  = 2000000;

// Note: The API functions is at the botton of the file.  Those functions call methods of the
// singleton SwappyVk class.  Those methods call virtual methods of the abstract SwappyVkBase
// class, which is actually implemented by one of the derived/concrete classes:
//
// - SwappyVkGoogleDisplayTiming
// - SwappyVkVulkanFallback
// - SwappyVkAndroidFallback

// Forward declarations:
class SwappyVk;


/***************************************************************************************************
 *
 * Per-Device abstract base class.
 *
 ***************************************************************************************************/

/**
 * Abstract base class that calls the Vulkan API.
 *
 * It is expected that one concrete class will be instantiated per VkDevice, and that all
 * VkSwapchainKHR's for a given VkDevice will share the same instance.
 *
 * Base class members are used by the derived classes to unify the behavior across implementaitons:
 *  @mThread - Thread used for getting Choreographer events.
 *  @mTreadRunning - Used to signal the tread to exit
 *  @mNextPresentID - unique ID for frame presentation.
 *  @mNextDesiredPresentTime - Holds the time in nanoseconds for the next frame to be presented.
 *  @mNextPresentIDToCheck - Used to determine whether presentation time needs to be adjusted.
 *  @mFrameID - Keeps track of how many Choreographer callbacks received.
 *  @mLastframeTimeNanos - Holds the last frame time reported by Choreographer.
 *  @mSumRefreshTime - Used together with @mSamples to calculate refresh rate based on Choreographer.
 */
class SwappyVkBase
{
public:
    SwappyVkBase(VkPhysicalDevice physicalDevice,
                 VkDevice         device,
                 uint64_t         refreshDur,
                 uint32_t         interval,
                 SwappyVk         &swappyVk,
                 void             *libVulkan) :
            mPhysicalDevice(physicalDevice), mDevice(device), mRefreshDur(refreshDur),
            mInterval(interval), mSwappyVk(swappyVk), mLibVulkan(libVulkan), mInitialized(false)
    {
        mpfnGetDeviceProcAddr =
                reinterpret_cast<PFN_vkGetDeviceProcAddr>(
                    dlsym(mLibVulkan, "vkGetDeviceProcAddr"));
        mpfnQueuePresentKHR =
                reinterpret_cast<PFN_vkQueuePresentKHR>(
                    mpfnGetDeviceProcAddr(mDevice, "vkQueuePresentKHR"));
    }
    virtual bool doGetRefreshCycleDuration(VkSwapchainKHR swapchain,
                                           uint64_t*      pRefreshDuration) = 0;
    void doSetSwapInterval(VkSwapchainKHR swapchain,
                           uint32_t       interval)
    {
        mInterval = interval;
    }
    virtual VkResult doQueuePresent(VkQueue                 queue,
                                    const VkPresentInfoKHR* pPresentInfo) = 0;
protected:
    VkPhysicalDevice mPhysicalDevice;
    VkDevice         mDevice;
    uint64_t         mRefreshDur;
    uint32_t         mInterval;
    SwappyVk         &mSwappyVk;
    void             *mLibVulkan;
    bool             mInitialized;
    pthread_t mThread = 0;
    ALooper *mLooper = nullptr;
    bool mTreadRunning = false;
    AChoreographer *mChoreographer = nullptr;
    std::mutex mWaitingMutex;
    std::condition_variable mWaitingCondition;
    uint32_t mNextPresentID = 0;
    uint64_t mNextDesiredPresentTime = 0;
    uint32_t mNextPresentIDToCheck = 2;

    PFN_vkGetDeviceProcAddr mpfnGetDeviceProcAddr = nullptr;
    PFN_vkQueuePresentKHR   mpfnQueuePresentKHR = nullptr;
    PFN_vkGetRefreshCycleDurationGOOGLE mpfnGetRefreshCycleDurationGOOGLE = nullptr;
    PFN_vkGetPastPresentationTimingGOOGLE mpfnGetPastPresentationTimingGOOGLE = nullptr;

    long mFrameID = 0;
    long mLastframeTimeNanos = 0;
    long mSumRefreshTime = 0;
    long mSamples = 0;

    static constexpr int CHOREOGRAPHER_THRESH = 1000;
    static constexpr int MAX_SAMPLES = 5;

    void initGoogExtention()
    {
        mpfnGetRefreshCycleDurationGOOGLE =
                reinterpret_cast<PFN_vkGetRefreshCycleDurationGOOGLE>(
                        mpfnGetDeviceProcAddr(mDevice, "vkGetRefreshCycleDurationGOOGLE"));
        mpfnGetPastPresentationTimingGOOGLE =
                reinterpret_cast<PFN_vkGetPastPresentationTimingGOOGLE>(
                        mpfnGetDeviceProcAddr(mDevice, "vkGetPastPresentationTimingGOOGLE"));
    }

    void startChoreographerThread();
    void stopChoreographerThread();
    static void *looperThreadWrapper(void *data);
    void *looperThread();
    static void frameCallback(long frameTimeNanos, void *data);
    void onDisplayRefresh(long frameTimeNanos);
    void calcRefreshRate(long frameTimeNanos);
};

void SwappyVkBase::startChoreographerThread() {
    std::unique_lock<std::mutex> lock(mWaitingMutex);
    // create a new ALooper thread to get Choreographer events
    mTreadRunning = true;
    pthread_create(&mThread, NULL, looperThreadWrapper, this);
    mWaitingCondition.wait(lock, [&]() { return mChoreographer != nullptr; });
}

void SwappyVkBase::stopChoreographerThread() {
    if (mLooper) {
        ALooper_acquire(mLooper);
        mTreadRunning = false;
        ALooper_wake(mLooper);
        ALooper_release(mLooper);
        pthread_join(mThread, NULL);
    }
}

void *SwappyVkBase::looperThreadWrapper(void *data) {
    SwappyVkBase *me = reinterpret_cast<SwappyVkBase *>(data);
    return me->looperThread();
}

void *SwappyVkBase::looperThread() {
    int outFd, outEvents;
    void *outData;

    mLooper = ALooper_prepare(0);
    if (!mLooper) {
        ALOGE("ALooper_prepare failed");
        return NULL;
    }

    mChoreographer = AChoreographer_getInstance();
    if (!mChoreographer) {
        ALOGE("AChoreographer_getInstance failed");
        return NULL;
    }
    mWaitingCondition.notify_all();

    while (mTreadRunning) {
        ALooper_pollAll(-1, &outFd, &outEvents, &outData);
    }

    return NULL;
}

void SwappyVkBase::frameCallback(long frameTimeNanos, void *data) {
    SwappyVkBase *me = reinterpret_cast<SwappyVkBase *>(data);
    me->onDisplayRefresh(frameTimeNanos);
}

void SwappyVkBase::onDisplayRefresh(long frameTimeNanos) {
    std::lock_guard<std::mutex> lock(mWaitingMutex);
    calcRefreshRate(frameTimeNanos);
    mLastframeTimeNanos = frameTimeNanos;
    mFrameID++;
    mWaitingCondition.notify_all();
}

void SwappyVkBase::calcRefreshRate(long frameTimeNanos) {
    long refresh_nano = abs(frameTimeNanos - mLastframeTimeNanos);

    if (mRefreshDur != 0 || mLastframeTimeNanos == 0) {
        return;
    }

    // ignore wrap around
    if (mLastframeTimeNanos > frameTimeNanos) {
        return;
    }

    mSumRefreshTime += refresh_nano;
    mSamples++;

    if (mSamples == MAX_SAMPLES) {
        mRefreshDur = mSumRefreshTime / mSamples;
    }
}


/***************************************************************************************************
 *
 * Per-Device concrete/derived class for using VK_GOOGLE_display_timing.
 *
 * This class uses the VK_GOOGLE_display_timing in order to present frames at a muliple (the "swap
 * interval") of a fixed refresh-cycle duration (i.e. the time between successive vsync's).
 *
 * In order to reduce complexity, some simplifying assumptions are made:
 *
 * - We assume a fixed refresh-rate (FRR) display that's between 60 Hz and 120 Hz.
 *
 * - While Vulkan allows applications to create and use multiple VkSwapchainKHR's per VkDevice, and
 *   to re-create VkSwapchainKHR's, we assume that the application uses a single VkSwapchainKHR,
 *   and never re-creates it.
 *
 * - The values reported back by the VK_GOOGLE_display_timing extension (which comes from
 *   lower-level Android interfaces) are not precise, and that values can drift over time.  For
 *   example, the refresh-cycle duration for a 60 Hz display should be 16,666,666 nsec; but the
 *   value reported back by the extension won't be precisely this.  Also, the differences betweeen
 *   the times of two successive frames won't be an exact multiple of 16,666,666 nsec.  This can
 *   make it difficult to precisely predict when a future vsync will be (it can appear to drift
 *   overtime).  Therefore, we try to give a desiredPresentTime for each image that is between 3
 *   and 7 msec before vsync.  We look at the actualPresentTime for previously-presented images,
 *   and nudge the future desiredPresentTime back within those 3-7 msec boundaries.
 *
 * - There can be a few frames of latency between when an image is presented and when the
 *   actualPresentTime is available for that image.  Therefore, we initially just pick times based
 *   upon CLOCK_MONOTONIC (which is the time domain for VK_GOOGLE_display_timing).  After we get
 *   past-present times, we nudge the desiredPresentTime, we wait for a few presents before looking
 *   again to see whether we need to nudge again.
 *
 * - If, for some reason, an application can't keep up with its chosen swap interval (e.g. it's
 *   designed for 30FPS on a premium device and is now running on a slow device; or it's running on
 *   a 120Hz display), this algorithm may not be able to make up for this (i.e. smooth rendering at
 *   a targetted frame rate may not be possible with an application that can't render fast enough).
 *
 ***************************************************************************************************/

/**
 * Concrete/derived class that sits on top of VK_GOOGLE_display_timing
 */
class SwappyVkGoogleDisplayTiming : public SwappyVkBase
{
public:
    SwappyVkGoogleDisplayTiming(VkPhysicalDevice physicalDevice,
                                VkDevice         device,
                                SwappyVk         &swappyVk,
                                void             *libVulkan) :
            SwappyVkBase(physicalDevice, device, k16_6msec, 1, swappyVk, libVulkan)
    {
        initGoogExtention();
    }
    virtual bool doGetRefreshCycleDuration(VkSwapchainKHR swapchain,
                                           uint64_t*      pRefreshDuration) override
    {
        VkRefreshCycleDurationGOOGLE refreshCycleDuration;
        VkResult res = mpfnGetRefreshCycleDurationGOOGLE(mDevice, swapchain, &refreshCycleDuration);
        if (res != VK_SUCCESS) {
            // This should never occur, but in case it does, return 16,666,666ns:
            mRefreshDur = k16_6msec;
        } else {
            mRefreshDur = refreshCycleDuration.refreshDuration;
        }

        // TEMP CODE: LOG REFRESH DURATION AND RATE:
        double refreshRate = mRefreshDur;
        refreshRate = 1.0 / (refreshRate / 1000000000.0);
        ALOGD("Returning refresh duration of %llu nsec (approx %f Hz)", mRefreshDur, refreshRate);

        *pRefreshDuration = mRefreshDur;
        return true;
    }
    virtual VkResult doQueuePresent(VkQueue                 queue,
                                    const VkPresentInfoKHR* pPresentInfo) override;

private:
    void calculateNextDesiredPresentTime(VkSwapchainKHR swapchain);
    void checkPastPresentTiming(VkSwapchainKHR swapchain);
};

VkResult SwappyVkGoogleDisplayTiming::doQueuePresent(VkQueue                 queue,
                                                     const VkPresentInfoKHR* pPresentInfo)
{
    VkResult ret = VK_SUCCESS;

    calculateNextDesiredPresentTime(pPresentInfo->pSwapchains[0]);

    // Setup the new structures to pass:
    VkPresentTimeGOOGLE *pPresentTimes =
            reinterpret_cast<VkPresentTimeGOOGLE*>(malloc(sizeof(VkPresentTimeGOOGLE) *
                                                          pPresentInfo->swapchainCount));
    for (uint32_t i = 0 ; i < pPresentInfo->swapchainCount ; i++) {
        pPresentTimes[i].presentID = mNextPresentID;
        pPresentTimes[i].desiredPresentTime = mNextDesiredPresentTime;
    }
    mNextPresentID++;

    VkPresentTimesInfoGOOGLE presentTimesInfo = {VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE,
                                                 pPresentInfo->pNext, pPresentInfo->swapchainCount,
                                                 pPresentTimes};
    VkPresentInfoKHR replacementPresentInfo = {pPresentInfo->sType, &presentTimesInfo,
                                               pPresentInfo->waitSemaphoreCount,
                                               pPresentInfo->pWaitSemaphores,
                                               pPresentInfo->swapchainCount,
                                               pPresentInfo->pSwapchains,
                                               pPresentInfo->pImageIndices, pPresentInfo->pResults};
    ret = mpfnQueuePresentKHR(queue, &replacementPresentInfo);
    free(pPresentTimes);
    return ret;
}

void SwappyVkGoogleDisplayTiming::calculateNextDesiredPresentTime(VkSwapchainKHR swapchain)
{
    struct timespec currTime;
    clock_gettime(CLOCK_MONOTONIC, &currTime);
    uint64_t currentTime =
            ((uint64_t) currTime.tv_sec * kBillion) + (uint64_t) currTime.tv_nsec;


    // Determine the desiredPresentTime:
    if (!mNextDesiredPresentTime) {
        mNextDesiredPresentTime = currentTime + mRefreshDur;
    } else {
        // Look at the timing of past presents, and potentially adjust mNextDesiredPresentTime:
        checkPastPresentTiming(swapchain);
        mNextDesiredPresentTime += mRefreshDur * mInterval;

        // Make sure the calculated time is not before the current time to present
        if (mNextDesiredPresentTime < currentTime) {
            mNextDesiredPresentTime = currentTime + mRefreshDur;
        }
    }
}

void SwappyVkGoogleDisplayTiming::checkPastPresentTiming(VkSwapchainKHR swapchain)
{
    VkResult ret = VK_SUCCESS;

    if (mNextPresentID <= mNextPresentIDToCheck) {
        return;
    }
    // Check the timing of past presents to see if we need to adjust mNextDesiredPresentTime:
    uint32_t pastPresentationTimingCount = 0;
    VkResult err = mpfnGetPastPresentationTimingGOOGLE(mDevice, swapchain,
                                                       &pastPresentationTimingCount, NULL);
    if (!pastPresentationTimingCount) {
        return;
    }
    // TODO: don't allocate memory for the timestamps every time.
    VkPastPresentationTimingGOOGLE *past =
            reinterpret_cast<VkPastPresentationTimingGOOGLE*>(
                    malloc(sizeof(VkPastPresentationTimingGOOGLE) *
                           pastPresentationTimingCount));
    err = mpfnGetPastPresentationTimingGOOGLE(mDevice, swapchain,
                                              &pastPresentationTimingCount, past);
    for (uint32_t i = 0; i < pastPresentationTimingCount; i++) {
        // Note: On Android, actualPresentTime can actually be before desiredPresentTime
        // (which shouldn't be possible.  Therefore, this must be a signed integer.
        int64_t amountEarlyBy =
                (int64_t) past[i].actualPresentTime - (int64_t)past[i].desiredPresentTime;
        if (amountEarlyBy < kTooCloseToVsyncBoundary) {
            // We're getting too close to vsync.  Nudge the next present back
            // towards/in the boundaries, and check back after a few more presents:
            mNextDesiredPresentTime -= kNudgeWithinVsyncBoundaries;
            mNextPresentIDToCheck = mNextPresentID + 7;
            break;
        }
        if (amountEarlyBy > kTooFarAwayFromVsyncBoundary) {
            // We're getting too far away from vsync.  Nudge the next present back
            // towards/in the boundaries, and check back after a few more presents:
            mNextDesiredPresentTime += kNudgeWithinVsyncBoundaries;
            mNextPresentIDToCheck = mNextPresentID + 7;
            break;
        }
    }
    free(past);
}

/**
 * Concrete/derived class that sits on top of VK_GOOGLE_display_timing
 */
class SwappyVkGoogleDisplayTimingAndroid : public SwappyVkGoogleDisplayTiming
{
public:
    SwappyVkGoogleDisplayTimingAndroid(VkPhysicalDevice physicalDevice,
                                VkDevice         device,
                                SwappyVk         &swappyVk,
                                void             *libVulkan) :
            SwappyVkGoogleDisplayTiming(physicalDevice, device, swappyVk,libVulkan) {
        startChoreographerThread();
    }

    ~SwappyVkGoogleDisplayTimingAndroid() {
        stopChoreographerThread();
    }

    virtual VkResult doQueuePresent(VkQueue                 queue,
                                    const VkPresentInfoKHR* pPresentInfo) override;

};

VkResult SwappyVkGoogleDisplayTimingAndroid::doQueuePresent(VkQueue                 queue,
                                                     const VkPresentInfoKHR* pPresentInfo)
{
    VkResult ret = VK_SUCCESS;
    struct timespec currTime;
    clock_gettime(CLOCK_MONOTONIC, &currTime);
    uint64_t currentTime =
            ((uint64_t) currTime.tv_sec * kBillion) + (uint64_t) currTime.tv_nsec;

    // do we have something in the queue ?
    if (mNextDesiredPresentTime > currentTime) {
        std::unique_lock<std::mutex> lock(mWaitingMutex);
        long target = mFrameID + mInterval;
        mWaitingCondition.wait(lock, [&]() {
            if (mFrameID < target) {
                // wait for the next frame as this frame is too soon
                AChoreographer_postFrameCallbackDelayed(mChoreographer, frameCallback, this, 1);
                return false;
            }
            return true;
        });
    }
    clock_gettime(CLOCK_MONOTONIC, &currTime);
    currentTime =
            ((uint64_t) currTime.tv_sec * kBillion) + (uint64_t) currTime.tv_nsec;
    mNextDesiredPresentTime = currentTime + mRefreshDur * mInterval;


    // Setup the new structures to pass:
    VkPresentTimeGOOGLE pPresentTimes[pPresentInfo->swapchainCount];
    for (uint32_t i = 0 ; i < pPresentInfo->swapchainCount ; i++) {
        pPresentTimes[i].presentID = mNextPresentID;
        pPresentTimes[i].desiredPresentTime = mNextDesiredPresentTime;
    }
    mNextPresentID++;

    VkPresentTimesInfoGOOGLE presentTimesInfo = {VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE,
                                                 pPresentInfo->pNext, pPresentInfo->swapchainCount,
                                                 pPresentTimes};
    VkPresentInfoKHR replacementPresentInfo = {pPresentInfo->sType, &presentTimesInfo,
                                               pPresentInfo->waitSemaphoreCount,
                                               pPresentInfo->pWaitSemaphores,
                                               pPresentInfo->swapchainCount,
                                               pPresentInfo->pSwapchains,
                                               pPresentInfo->pImageIndices, pPresentInfo->pResults};
    ret = mpfnQueuePresentKHR(queue, &replacementPresentInfo);

    return ret;
}

/***************************************************************************************************
 *
 * Per-Device concrete/derived class for the "Android fallback" path (uses
 * Choreographer to try to get presents to occur at the desired time).
 *
 ***************************************************************************************************/

/**
 * Concrete/derived class that sits on top of the Vulkan API
 */
#ifdef ANDROID
class SwappyVkAndroidFallback : public SwappyVkBase
{
public:
    SwappyVkAndroidFallback(VkPhysicalDevice physicalDevice,
                            VkDevice         device,
                            SwappyVk         &swappyVk,
                            void             *libVulkan) :
            SwappyVkBase(physicalDevice, device, 0, 1, swappyVk, libVulkan) {
        startChoreographerThread();
    }

        ~SwappyVkAndroidFallback() {
            stopChoreographerThread();
    }

    virtual bool doGetRefreshCycleDuration(VkSwapchainKHR swapchain,
                                               uint64_t*      pRefreshDuration) override
    {
        std::unique_lock<std::mutex> lock(mWaitingMutex);
        mWaitingCondition.wait(lock, [&]() {
            if (mRefreshDur == 0) {
                AChoreographer_postFrameCallbackDelayed(mChoreographer, frameCallback, this, 1);
                return false;
            }
            return true;
        });

        *pRefreshDuration = mRefreshDur;

        double refreshRate = mRefreshDur;
        refreshRate = 1.0 / (refreshRate / 1000000000.0);
        ALOGI("Returning refresh duration of %llu nsec (approx %f Hz)", mRefreshDur, refreshRate);
        return true;
    }

    virtual VkResult doQueuePresent(VkQueue                 queue,
                                    const VkPresentInfoKHR* pPresentInfo) override
    {
        {
            const long target = mFrameID + mInterval;
            std::unique_lock<std::mutex> lock(mWaitingMutex);


            mWaitingCondition.wait(lock, [&]() {
                if (mFrameID < target) {
                    // wait for the next frame as this frame is too soon
                    AChoreographer_postFrameCallbackDelayed(mChoreographer, frameCallback, this, 1);
                    return false;
                }
                return true;
            });
        }
        return mpfnQueuePresentKHR(queue, pPresentInfo);
    }
};
#endif

/***************************************************************************************************
 *
 * Per-Device concrete/derived class for the "Vulkan fallback" path (i.e. no API/OS timing support;
 * just generic Vulkan)
 *
 ***************************************************************************************************/

/**
 * Concrete/derived class that sits on top of the Vulkan API
 */
class SwappyVkVulkanFallback : public SwappyVkBase
{
public:
    SwappyVkVulkanFallback(VkPhysicalDevice physicalDevice,
                            VkDevice         device,
                            SwappyVk         &swappyVk,
                            void             *libVulkan) :
            SwappyVkBase(physicalDevice, device, k16_6msec, 1, swappyVk, libVulkan) {}
    virtual bool doGetRefreshCycleDuration(VkSwapchainKHR swapchain,
                                           uint64_t*      pRefreshDuration) override
    {
        *pRefreshDuration = mRefreshDur;
        return true;
    }
    virtual VkResult doQueuePresent(VkQueue                 queue,
                                    const VkPresentInfoKHR* pPresentInfo) override
    {
        return mpfnQueuePresentKHR(queue, pPresentInfo);
    }
};




/***************************************************************************************************
 *
 * Singleton class that provides the high-level implementation of the Swappy entrypoints.
 *
 ***************************************************************************************************/
/**
 * Singleton class that provides the high-level implementation of the Swappy entrypoints.
 *
 * This class determines which low-level implementation to use for each physical
 * device, and then calls that class's do-method for the entrypoint.
 */
class SwappyVk
{
public:
    static SwappyVk& getInstance()
    {
        static SwappyVk instance;
        return instance;
    }
    ~SwappyVk() {}

    void swappyVkDetermineDeviceExtensions(VkPhysicalDevice       physicalDevice,
                                           uint32_t               availableExtensionCount,
                                           VkExtensionProperties* pAvailableExtensions,
                                           uint32_t*              pRequiredExtensionCount,
                                           char**                 pRequiredExtensions);
    bool GetRefreshCycleDuration(VkPhysicalDevice physicalDevice,
                                 VkDevice         device,
                                 VkSwapchainKHR   swapchain,
                                 uint64_t*        pRefreshDuration);
    void SetSwapInterval(VkDevice       device,
                         VkSwapchainKHR swapchain,
                         uint32_t       interval);
    VkResult QueuePresent(VkQueue                 queue,
                          const VkPresentInfoKHR* pPresentInfo);

private:
    std::map<VkPhysicalDevice, bool> doesPhysicalDeviceHaveGoogleDisplayTiming;
    std::map<VkDevice, SwappyVkBase*> perDeviceImplementation;
    std::map<VkSwapchainKHR, SwappyVkBase*> perSwapchainImplementation;

    void *mLibVulkan     = nullptr;

private:
    SwappyVk() {} // Need to implement this constructor
    SwappyVk(SwappyVk const&); // Don't implement a copy constructor--no copies
    void operator=(SwappyVk const&); // Don't implement--no copies
};


/**
 * Generic/Singleton implementation of swappyVkDetermineDeviceExtensions.
 */
void SwappyVk::swappyVkDetermineDeviceExtensions(
    VkPhysicalDevice       physicalDevice,
    uint32_t               availableExtensionCount,
    VkExtensionProperties* pAvailableExtensions,
    uint32_t*              pRequiredExtensionCount,
    char**                 pRequiredExtensions)
{
    // TODO: Refactor this to be more concise:
    if (!pRequiredExtensions) {
        for (uint32_t i = 0; i < availableExtensionCount; i++) {
            if (!strcmp(VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME,
                        pAvailableExtensions[i].extensionName)) {
                (*pRequiredExtensionCount)++;
            }
        }
    } else {
        doesPhysicalDeviceHaveGoogleDisplayTiming[physicalDevice] = false;
        for (uint32_t i = 0, j = 0; i < availableExtensionCount; i++) {
            if (!strcmp(VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME,
                        pAvailableExtensions[i].extensionName)) {
                if (j < *pRequiredExtensionCount) {
                    strcpy(pRequiredExtensions[j++], VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME);
                    doesPhysicalDeviceHaveGoogleDisplayTiming[physicalDevice] = true;
                }
            }
        }
    }
}


/**
 * Generic/Singleton implementation of swappyVkGetRefreshCycleDuration.
 */
bool SwappyVk::GetRefreshCycleDuration(VkPhysicalDevice physicalDevice,
                                       VkDevice         device,
                                       VkSwapchainKHR   swapchain,
                                       uint64_t*        pRefreshDuration)
{
    SwappyVkBase *pImplementation = perDeviceImplementation[device];
    if (!pImplementation) {
        // We have not seen this device yet.
        if (!mLibVulkan) {
            // This is the first time we've been called--initialize function pointers:
            mLibVulkan = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
            if (!mLibVulkan)
            {
                // If Vulkan doesn't exist, bail-out early:
                return false;
            }
        }

        // First, based on whether VK_GOOGLE_display_timing is available
        // (determined and cached by swappyVkDetermineDeviceExtensions),
        // determine which derived class to use to implement the rest of the API
        if (doesPhysicalDeviceHaveGoogleDisplayTiming[physicalDevice]) {
#ifdef ANDROID
            pImplementation = new SwappyVkGoogleDisplayTimingAndroid(physicalDevice, device,
                                                              getInstance(), mLibVulkan);
            ALOGV("SwappyVk initialized for VkDevice %p using VK_GOOGLE_display_timing on Android", device);
#else
            // Instantiate the class that sits on top of VK_GOOGLE_display_timing
            pImplementation = new SwappyVkGoogleDisplayTiming(physicalDevice, device,
                                                              getInstance(), mLibVulkan);
            ALOGV("SwappyVk initialized for VkDevice %p using VK_GOOGLE_display_timing", device);
#endif
        } else {
            // Instantiate the class that sits on top of the basic Vulkan APIs
#ifdef ANDROID
            pImplementation = new SwappyVkAndroidFallback(physicalDevice, device, getInstance(),
                                                          mLibVulkan);
            ALOGV("SwappyVk initialized for VkDevice %p using Android fallback", device);
#else  // ANDROID
            pImplementation = new SwappyVkVulkanFallback(physicalDevice, device, getInstance(),
                                                         mLibVulkan);
            ALOGV("SwappyVk initialized for VkDevice %p using Vulkan-only fallback", device);
#endif // ANDROID
        }

        // Second, cache the per-device pointer to the derived class:
        if (pImplementation) {
            perDeviceImplementation[device] = pImplementation;
        } else {
            // This shouldn't happen, but if it does, something is really wrong.
            return false;
        }
    }

    // Cache the per-swapchain pointer to the derived class:
    perSwapchainImplementation[swapchain] = pImplementation;

    // Now, call that derived class to get the refresh duration to return
    return pImplementation->doGetRefreshCycleDuration(swapchain, pRefreshDuration);
}


/**
 * Generic/Singleton implementation of swappyVkSetSwapInterval.
 */
void SwappyVk::SetSwapInterval(VkDevice       device,
                               VkSwapchainKHR swapchain,
                               uint32_t       interval)
{
    SwappyVkBase *pImplementation = perDeviceImplementation[device];
    if (!pImplementation) {
        return;
    }
    pImplementation->doSetSwapInterval(swapchain, interval);
}


/**
 * Generic/Singleton implementation of swappyVkQueuePresent.
 */
VkResult SwappyVk::QueuePresent(VkQueue                 queue,
                                const VkPresentInfoKHR* pPresentInfo)
{
    // This command doesn't have a VkDevice.  It should have at least one VkSwapchainKHR's.  For
    // this command, all VkSwapchainKHR's will have the same VkDevice and VkQueue.
    if ((pPresentInfo->swapchainCount == 0) || (!pPresentInfo->pSwapchains)) {
        // This shouldn't happen, but if it does, something is really wrong.
        return VK_ERROR_DEVICE_LOST;
    }
    SwappyVkBase *pImplementation = perSwapchainImplementation[*pPresentInfo->pSwapchains];
    if (pImplementation) {
        return pImplementation->doQueuePresent(queue, pPresentInfo);
    } else {
        // This should only happen if the API was used wrong (e.g. they never
        // called swappyVkGetRefreshCycleDuration).
        // NOTE: Technically, a Vulkan library shouldn't protect a user from
        // themselves, but we'll be friendlier
        return VK_ERROR_DEVICE_LOST;
    }
}


/***************************************************************************************************
 *
 * API ENTRYPOINTS
 *
 ***************************************************************************************************/

extern "C" {

void swappyVkDetermineDeviceExtensions(
    VkPhysicalDevice       physicalDevice,
    uint32_t               availableExtensionCount,
    VkExtensionProperties* pAvailableExtensions,
    uint32_t*              pRequiredExtensionCount,
    char**                 pRequiredExtensions)
{
    ATRACE_CALL();
    SwappyVk& swappy = SwappyVk::getInstance();
    swappy.swappyVkDetermineDeviceExtensions(physicalDevice,
                                             availableExtensionCount, pAvailableExtensions,
                                             pRequiredExtensionCount, pRequiredExtensions);
}

bool swappyVkGetRefreshCycleDuration(
        VkPhysicalDevice physicalDevice,
        VkDevice         device,
        VkSwapchainKHR   swapchain,
        uint64_t*        pRefreshDuration)
{
    ATRACE_CALL();
    SwappyVk& swappy = SwappyVk::getInstance();
    return swappy.GetRefreshCycleDuration(physicalDevice, device, swapchain, pRefreshDuration);
}

void swappyVkSetSwapInterval(
        VkDevice       device,
        VkSwapchainKHR swapchain,
        uint32_t       interval)
{
    ATRACE_CALL();
    SwappyVk& swappy = SwappyVk::getInstance();
    swappy.SetSwapInterval(device, swapchain, interval);
}

VkResult swappyVkQueuePresent(
        VkQueue                 queue,
        const VkPresentInfoKHR* pPresentInfo)
{
    ATRACE_CALL();
    SwappyVk& swappy = SwappyVk::getInstance();
    return swappy.QueuePresent(queue, pPresentInfo);
}

}  // extern "C"
