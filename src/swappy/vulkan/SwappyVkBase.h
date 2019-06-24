/*
 * Copyright 2019 The Android Open Source Project
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
 * Base class members are used by the derived classes to unify the behavior across implementations:
 *  @mThread - Thread used for getting Choreographer events.
 *  @mTreadRunning - Used to signal the tread to exit
 *  @mNextPresentID - unique ID for frame presentation.
 *  @mNextDesiredPresentTime - Holds the time in nanoseconds for the next frame to be presented.
 *  @mNextPresentIDToCheck - Used to determine whether presentation time needs to be adjusted.
 *  @mFrameID - Keeps track of how many Choreographer callbacks received.
 *  @mLastframeTimeNanos - Holds the last frame time reported by Choreographer.
 *  @mSumRefreshTime - Used together with @mSamples to calculate refresh rate based on Choreographer.
 */

#pragma once

#define SWAPPYVK_USE_WRAPPER
#include <swappy/swappyVk.h>

#include <unistd.h>
#include <pthread.h>
#include <dlfcn.h>
#include <inttypes.h>
#include <cstdlib>
#include <cstring>

#include <map>
#include <condition_variable>
#include <mutex>
#include <list>

#include "SwappyCommon.h"
#include "Settings.h"
#include "Trace.h"
#include "ChoreographerShim.h"

namespace swappy {

#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, "SwappyVk", __VA_ARGS__)
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN, "SwappyVk", __VA_ARGS__)
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, "SwappyVk", __VA_ARGS__)
#define ALOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "SwappyVk", __VA_ARGS__)
#define ALOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, "SwappyVk", __VA_ARGS__)

constexpr uint32_t kThousand = 1000;
constexpr uint32_t kMillion  = 1000000;
constexpr uint32_t kBillion  = 1000000000;
constexpr uint32_t k16_6msec = 16666666;

constexpr uint32_t kTooCloseToVsyncBoundary     = 3000000;
constexpr uint32_t kTooFarAwayFromVsyncBoundary = 7000000;
constexpr uint32_t kNudgeWithinVsyncBoundaries  = 2000000;

// AChoreographer is supported from API 24. To allow compilation for minSDK < 24
// and still use AChoreographer for SDK >= 24 we need runtime support to call
// AChoreographer APIs.

using PFN_AChoreographer_getInstance = AChoreographer* (*)();

using PFN_AChoreographer_postFrameCallback = void (*)(AChoreographer* choreographer,
                                                      AChoreographer_frameCallback callback,
                                                      void* data);

using PFN_AChoreographer_postFrameCallbackDelayed = void (*)(AChoreographer* choreographer,
                                                             AChoreographer_frameCallback callback,
                                                             void* data,
                                                             long delayMillis);

class SwappyVkBase
{
public:
    SwappyVkBase(JNIEnv           *env,
                 jobject          jactivity,
                 VkPhysicalDevice physicalDevice,
                 VkDevice         device,
                 void             *libVulkan);

    virtual bool doGetRefreshCycleDuration(VkSwapchainKHR swapchain,
                                           uint64_t*      pRefreshDuration) = 0;

    virtual VkResult doQueuePresent(VkQueue                 queue,
                                    uint32_t                queueFamilyIndex,
                                    const VkPresentInfoKHR* pPresentInfo) = 0;

    void doSetSwapInterval(VkSwapchainKHR swapchain,
                           uint64_t       swap_ns);

    VkResult injectFence(VkQueue                 queue,
                         const VkPresentInfoKHR* pPresentInfo,
                         VkSemaphore*            pSemaphore);

    bool isEnabled() { return mEnabled; }

    void setAutoSwapInterval(bool enabled);
    void setAutoPipelineMode(bool enabled);

    void setMaxAutoSwapIntervalNS(std::chrono::nanoseconds swapMaxNS);

    void setFenceTimeout(std::chrono::nanoseconds duration);
    std::chrono::nanoseconds getFenceTimeout() const;

    void addTracer(const SwappyTracer *tracer);

protected:
    struct VkSync {
        VkFence fence;
        VkSemaphore semaphore;
        VkCommandBuffer command;
        VkEvent event;
    };

    struct ThreadContext {
        ThreadContext(std::thread t) : thread(std::move(t)) {}

        std::thread thread;
        bool running GUARDED_BY(lock) = true;
        bool hasPendingWork GUARDED_BY(lock);
        std::mutex lock;
        std::condition_variable_any condition;
    };

    SwappyCommon     mCommonBase;
    VkPhysicalDevice mPhysicalDevice;
    VkDevice         mDevice;
    void*            mLibVulkan;
    bool             mInitialized;
    bool             mEnabled;

    uint32_t mNextPresentID = 0;
    uint32_t mNextPresentIDToCheck = 2;

    PFN_vkGetDeviceProcAddr               mpfnGetDeviceProcAddr               = nullptr;
    PFN_vkQueuePresentKHR                 mpfnQueuePresentKHR                 = nullptr;
    PFN_vkGetRefreshCycleDurationGOOGLE   mpfnGetRefreshCycleDurationGOOGLE   = nullptr;
    PFN_vkGetPastPresentationTimingGOOGLE mpfnGetPastPresentationTimingGOOGLE = nullptr;

    // Holds VKSync objects ready to be used
    std::map<VkQueue, std::list<VkSync>>              mFreeSyncPool;

    // Holds VKSync objects queued and but signaled yet
    std::map<VkQueue, std::list<VkSync>>              mWaitingSyncs;

    // Holds VKSync objects that were signaled
    std::map<VkQueue, std::list<VkSync>>              mSignaledSyncs;

    std::map<VkQueue, VkCommandPool>                  mCommandPool;
    std::map<VkQueue, std::unique_ptr<ThreadContext>> mThreads;

    static constexpr int MAX_PENDING_FENCES = 2;

    std::atomic<std::chrono::nanoseconds> mLastFenceTime = {};

    void initGoogExtension();
    VkResult initializeVkSyncObjects(VkQueue queue, uint32_t queueFamilyIndex);
    void destroyVkSyncObjects();
    bool lastFrameIsCompleted(VkQueue queue);
    std::chrono::nanoseconds getLastFenceTime(VkQueue queue);
    void waitForFenceThreadMain(VkQueue queue);
};

}  // namespace swappy
