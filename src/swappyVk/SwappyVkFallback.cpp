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

#include "SwappyVkFallback.h"

 /***************************************************************************************************
 *
 * Per-Device concrete/derived class for the "Android fallback" path (uses
 * Choreographer to try to get presents to occur at the desired time).
 *
 ***************************************************************************************************/

/**
 * Concrete/derived class that sits on top of the Vulkan API
 */
SwappyVkFallback::SwappyVkFallback(VkPhysicalDevice physicalDevice,
                                   VkDevice         device,
                                   SwappyVk         &swappyVk,
                                   void             *libVulkan) :
    SwappyVkBase(physicalDevice, device, 0, 1, swappyVk, libVulkan)
{
    startChoreographerThread();
}

SwappyVkFallback::~SwappyVkFallback() {
    stopChoreographerThread();
}

bool SwappyVkFallback::doGetRefreshCycleDuration(VkSwapchainKHR swapchain,
                                                 uint64_t*      pRefreshDuration)
{
    std::unique_lock<std::mutex> lock(mWaitingMutex);
    mWaitingCondition.wait(lock, [&]() {
        if (mRefreshDur == 0) {
            postChoreographerCallback();
            return false;
        }
        return true;
    });

    *pRefreshDuration = mRefreshDur;

    double refreshRate = mRefreshDur;
    refreshRate = 1.0 / (refreshRate / 1000000000.0);
    ALOGI("Returning refresh duration of %" PRIu64 " nsec (approx %f Hz)", mRefreshDur, refreshRate);
    return true;
}

VkResult SwappyVkFallback::doQueuePresent(VkQueue                 queue,
                                          uint32_t                queueFamilyIndex,
                                          const VkPresentInfoKHR* pPresentInfo)
{
    {
        std::unique_lock<std::mutex> lock(mWaitingMutex);

        mWaitingCondition.wait(lock, [&]() {
            if (mFrameID < mTargetFrameID) {
                postChoreographerCallback();
                return false;
            }
            return true;
        });
    }
    mTargetFrameID = mFrameID + mInterval;
    return mpfnQueuePresentKHR(queue, pPresentInfo);
}