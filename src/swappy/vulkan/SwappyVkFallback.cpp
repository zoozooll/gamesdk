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

#define LOG_TAG "SwappyVkFallback"

namespace swappy {

SwappyVkFallback::SwappyVkFallback(VkPhysicalDevice physicalDevice,
                                   VkDevice         device,
                                   void             *libVulkan) :
    SwappyVkBase(physicalDevice, device, libVulkan) {}

bool SwappyVkFallback::doGetRefreshCycleDuration(VkSwapchainKHR swapchain,
                                                 uint64_t*      pRefreshDuration) {
    // TODO(adyabr): get the app/sf offsets
    // TODO(adyabr): how to get the refresh duration here ?
    mCommonBase = std::make_unique<SwappyCommon>(nullptr, 16600000ns, 0ns, 0ns);

    // Since we don't have presentation timing, we cannot achieve pipelining.
    mCommonBase->setAutoPipelineMode(false);

    *pRefreshDuration = mCommonBase->getRefreshPeriod().count();

    double refreshRate = 1000000000.0 / *pRefreshDuration;
    ALOGI("Returning refresh duration of %" PRIu64 " nsec (approx %f Hz)",
        *pRefreshDuration, refreshRate);

    return true;
}

VkResult SwappyVkFallback::doQueuePresent(VkQueue                 queue,
                                          uint32_t                queueFamilyIndex,
                                          const VkPresentInfoKHR* pPresentInfo) {
    VkResult result = initializeVkSyncObjects(queue, queueFamilyIndex);
    if (result) {
        return result;
    }

    const SwappyCommon::SwapHandlers handlers = {
        .lastFrameIsComplete = std::bind(&SwappyVkFallback::lastFrameIsCompleted, this, queue),
        .getPrevFrameGpuTime = std::bind(&SwappyVkFallback::getLastFenceTime, this, queue),
    };

    // Inject the fence first and wait for it in onPreSwap() as we don't want to submit a frame
    // before rendering is completed.
    VkSemaphore semaphore;
    result = injectFence(queue, pPresentInfo, &semaphore);
    if (result) {
        ALOGE("Failed to vkQueueSubmit %d", result);
        return result;
    }

    mCommonBase->onPreSwap(handlers);

    VkPresentInfoKHR replacementPresentInfo = {
        pPresentInfo->sType,
        nullptr,
        1,
        &semaphore,
        pPresentInfo->swapchainCount,
        pPresentInfo->pSwapchains,
        pPresentInfo->pImageIndices,
        pPresentInfo->pResults
    };

    result = mpfnQueuePresentKHR(queue, pPresentInfo);

    mCommonBase->onPostSwap(handlers);

    return result;
}

}  // namespace swappy