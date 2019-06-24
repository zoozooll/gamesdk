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

#include "SwappyVkBase.h"

#include "SystemProperties.h"

#define LOG_TAG "SwappyVkBase"

namespace swappy {

SwappyVkBase::SwappyVkBase(JNIEnv           *env,
                           jobject          jactivity,
                           VkPhysicalDevice physicalDevice,
                           VkDevice         device,
                           void             *libVulkan) :
    mCommonBase(env, jactivity),
    mPhysicalDevice(physicalDevice),
    mDevice(device),
    mLibVulkan(libVulkan),
    mInitialized(false),
    mEnabled(false)
{
    if (!mCommonBase.isValid()) {
        ALOGE("SwappyCommon could not initialize correctly.");
        return;
    }

    InitVulkan();

    mpfnGetDeviceProcAddr =
            reinterpret_cast<PFN_vkGetDeviceProcAddr>(
                dlsym(mLibVulkan, "vkGetDeviceProcAddr"));
    mpfnQueuePresentKHR =
            reinterpret_cast<PFN_vkQueuePresentKHR>(
                mpfnGetDeviceProcAddr(mDevice, "vkQueuePresentKHR"));

    initGoogExtension();

    mEnabled = !getSystemPropViaGetAsBool(SWAPPY_SYSTEM_PROP_KEY_DISABLE, false);
}

void SwappyVkBase::initGoogExtension() {
    mpfnGetRefreshCycleDurationGOOGLE =
            reinterpret_cast<PFN_vkGetRefreshCycleDurationGOOGLE>(
                    mpfnGetDeviceProcAddr(mDevice, "vkGetRefreshCycleDurationGOOGLE"));
    mpfnGetPastPresentationTimingGOOGLE =
            reinterpret_cast<PFN_vkGetPastPresentationTimingGOOGLE>(
                    mpfnGetDeviceProcAddr(mDevice, "vkGetPastPresentationTimingGOOGLE"));
}

void SwappyVkBase::doSetSwapInterval(VkSwapchainKHR swapchain, uint64_t swap_ns) {
    Settings::getInstance()->setSwapIntervalNS(swap_ns);
}

VkResult SwappyVkBase::initializeVkSyncObjects(VkQueue   queue,
                                               uint32_t  queueFamilyIndex) {
    if (mCommandPool.find(queue) != mCommandPool.end()) {
        return VK_SUCCESS;
    }

    VkSync sync;

    const VkCommandPoolCreateInfo cmd_pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = NULL,
        .queueFamilyIndex = queueFamilyIndex,
        .flags = 0,
    };

    VkResult res = vkCreateCommandPool(mDevice, &cmd_pool_info, NULL, &mCommandPool[queue]);
    if (res) {
        ALOGE("vkCreateCommandPool failed %d", res);
        return res;
    }
    const VkCommandBufferAllocateInfo present_cmd_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = NULL,
        .commandPool = mCommandPool[queue],
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    for(int i = 0; i < MAX_PENDING_FENCES; i++) {
        VkFenceCreateInfo fence_ci = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0
        };
        res = vkCreateFence(mDevice, &fence_ci, NULL, &sync.fence);
        if (res) {
            ALOGE("failed to create fence: %d", res);
            return res;
        }

        VkSemaphoreCreateInfo semaphore_ci = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0
        };
        res = vkCreateSemaphore(mDevice, &semaphore_ci, NULL, &sync.semaphore);
        if (res) {
            ALOGE("failed to create semaphore: %d", res);
            return res;
        }

        res = vkAllocateCommandBuffers(mDevice, &present_cmd_info, &sync.command);
        if (res) {
            ALOGE("vkAllocateCommandBuffers failed %d", res);
            return res;
        }

        const VkCommandBufferBeginInfo cmd_buf_info = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .pNext = NULL,
                .flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
                .pInheritanceInfo = NULL,
        };
        res = vkBeginCommandBuffer(sync.command, &cmd_buf_info);
        if (res) {
            ALOGE("vkAllocateCommandBuffers failed %d", res);
            return res;
        }

        VkEventCreateInfo event_info = {
                .sType = VK_STRUCTURE_TYPE_EVENT_CREATE_INFO,
                .pNext = NULL,
                .flags = 0,
        };
        res = vkCreateEvent(mDevice, &event_info, NULL, &sync.event);
        if (res) {
            ALOGE("vkCreateEvent failed %d", res);
            return res;
        }

        vkCmdSetEvent(sync.command, sync.event, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

        res = vkEndCommandBuffer(sync.command);
        if (res) {
            ALOGE("vkCreateEvent failed %d", res);
            return res;
        }

        mFreeSyncPool[queue].push_back(sync);
    }

    // Create a thread that will wait for the fences
    mThreads.emplace(queue, std::make_unique<ThreadContext>(
            std::thread(std::bind(&SwappyVkBase::waitForFenceThreadMain, this, queue))));

    return VK_SUCCESS;
}

void SwappyVkBase::destroyVkSyncObjects() {
    // Stop all waiters threads
    for (auto it = mThreads.begin(); it != mThreads.end(); it++) {
        {
            std::lock_guard<std::mutex> lock(it->second->lock);
            it->second->running = false;
            it->second->condition.notify_one();
        }
        it->second->thread.join();
    }

    // Wait for all unsignaled fences to get signlaed
    for (auto it = mWaitingSyncs.begin(); it != mWaitingSyncs.end(); it++) {
        auto queue = it->first;
        auto syncList = it->second;
        while (syncList.size() > 0) {
            VkSync sync = syncList.front();
            syncList.pop_front();
            vkWaitForFences(mDevice, 1, &sync.fence, VK_TRUE,
                            mCommonBase.getFenceTimeout().count());
            vkResetFences(mDevice, 1, &sync.fence);
            mSignaledSyncs[queue].push_back(sync);
        }
    }

    // Move all signaled fences to the free pool
    for (auto it = mSignaledSyncs.begin(); it != mSignaledSyncs.end(); it++) {
        auto queue = it->first;
        auto syncList = it->second;
        while (syncList.size() > 0) {
            VkSync sync = syncList.front();
            syncList.pop_front();
            mFreeSyncPool[queue].push_back(sync);
        }
    }

    // Free all sync objects
    for (auto it = mFreeSyncPool.begin(); it != mFreeSyncPool.end(); it++) {
        auto queue = it->first;
        auto syncList = it->second;
        while (syncList.size() > 0) {
            VkSync sync = syncList.front();
            syncList.pop_front();
            vkFreeCommandBuffers(mDevice, mCommandPool[it->first], 1, &sync.command);
            vkDestroyEvent(mDevice, sync.event, NULL);
            vkDestroySemaphore(mDevice, sync.semaphore, NULL);
            vkDestroyFence(mDevice, sync.fence, NULL);
        }
    }

    // Free destroy the command pools
    for (auto it = mCommandPool.begin(); it != mCommandPool.end(); it++) {
        auto commandPool = it->second;
        vkDestroyCommandPool(mDevice, commandPool, NULL);
    }
}

bool SwappyVkBase::lastFrameIsCompleted(VkQueue queue) {
    auto pipelineMode = mCommonBase.getCurrentPipelineMode();
    std::lock_guard<std::mutex> lock(mThreads[queue]->lock);
    if (pipelineMode == SwappyCommon::PipelineMode::On) {
        // We are in pipeline mode so we need to check the fence of frame N-1
        const int numFencesSubmitted = MAX_PENDING_FENCES - mFreeSyncPool[queue].size();
        if (numFencesSubmitted < 2) {
            // First frame
            return true;
        }

        if (mSignaledSyncs[queue].size() == 0) {
            // There are no signaled fences
            return false;
        }

        VkSync &sync = mSignaledSyncs[queue].front();
        mSignaledSyncs[queue].pop_front();
        mFreeSyncPool[queue].push_back(sync);
        return true;
    } else {
        // We are not in pipeline mode so we need to check the fence the current frame. i.e. there
        // are not unsignaled frames
        while (mFreeSyncPool[queue].size() != MAX_PENDING_FENCES) {
            if (mSignaledSyncs[queue].size() == 0) {
                // There are no signaled fences
                return false;
            }
            VkSync &sync = mSignaledSyncs[queue].front();
            mSignaledSyncs[queue].pop_front();
            mFreeSyncPool[queue].push_back(sync);
        }
        return true;
    }
}

VkResult SwappyVkBase::injectFence(VkQueue                 queue,
                                   const VkPresentInfoKHR* pPresentInfo,
                                   VkSemaphore*            pSemaphore) {
    // If we cross the swap interval threshold, we don't pace at all.
    // In this case we might not have a free fence, so just don't use the fence.
    if (mFreeSyncPool[queue].size() == 0) {
        return VK_SUCCESS;
    }

    VkSync sync = mFreeSyncPool[queue].front();
    mFreeSyncPool[queue].pop_front();

    VkPipelineStageFlags pipe_stage_flags;
    VkSubmitInfo submit_info;
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.pNext = NULL;
    submit_info.pWaitDstStageMask = &pipe_stage_flags;
    pipe_stage_flags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    submit_info.waitSemaphoreCount = pPresentInfo->waitSemaphoreCount;
    submit_info.pWaitSemaphores = pPresentInfo->pWaitSemaphores;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &sync.command;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &sync.semaphore;
    VkResult res = vkQueueSubmit(queue, 1, &submit_info, sync.fence);
    *pSemaphore = sync.semaphore;

    std::lock_guard<std::mutex> lock(mThreads[queue]->lock);
    mWaitingSyncs[queue].push_back(sync);
    mThreads[queue]->hasPendingWork = true;
    mThreads[queue]->condition.notify_all();

    return res;
}

void SwappyVkBase::setAutoSwapInterval(bool enabled) {
    mCommonBase.setAutoSwapInterval(enabled);
}

void SwappyVkBase::setMaxAutoSwapIntervalNS(std::chrono::nanoseconds swapMaxNS) {
    mCommonBase.setMaxAutoSwapIntervalNS(swapMaxNS);
}

void SwappyVkBase::setAutoPipelineMode(bool enabled) {
    mCommonBase.setAutoPipelineMode(enabled);
}

void SwappyVkBase::waitForFenceThreadMain(VkQueue queue) {
    ThreadContext& thread = *mThreads[queue];

    while (true) {
        bool waitingSyncsEmpty;
        {
            std::lock_guard<std::mutex> lock(thread.lock);
            // Wait for new fence object
            thread.condition.wait(thread.lock, [&]() REQUIRES(thread.lock) {
                return thread.hasPendingWork || !thread.running;
            });

            thread.hasPendingWork = false;

            if (!thread.running) {
                break;
            }

            waitingSyncsEmpty = mWaitingSyncs[queue].empty();
        }

        while (!waitingSyncsEmpty) {
            VkSync sync;
            {  // Get the sync object with a lock
                std::lock_guard<std::mutex> lock(thread.lock);
                sync = mWaitingSyncs[queue].front();
                mWaitingSyncs[queue].pop_front();
            }

            const auto startTime = std::chrono::steady_clock::now();
            VkResult result = vkWaitForFences(mDevice, 1, &sync.fence, VK_TRUE,
                                              mCommonBase.getFenceTimeout().count());
            if (result) {
                ALOGE("Failed to wait for fence %d", result);
            }
            vkResetFences(mDevice, 1, &sync.fence);
            mLastFenceTime = std::chrono::steady_clock::now() - startTime;

            // Move the sync object to the signaled list
            {
                std::lock_guard<std::mutex> lock(thread.lock);

                mSignaledSyncs[queue].push_back(sync);
                waitingSyncsEmpty = mWaitingSyncs[queue].empty();
            }
        }
    }
}

std::chrono::nanoseconds SwappyVkBase::getLastFenceTime(VkQueue queue) {
    return mLastFenceTime;
}

void SwappyVkBase::setFenceTimeout(std::chrono::nanoseconds duration) {
    mCommonBase.setFenceTimeout(duration);
}

std::chrono::nanoseconds SwappyVkBase::getFenceTimeout() const {
    return mCommonBase.getFenceTimeout();
}

void SwappyVkBase::addTracer(const SwappyTracer *tracer) {
    mCommonBase.addTracerCallbacks(*tracer);
}

}  // namespace swappy
