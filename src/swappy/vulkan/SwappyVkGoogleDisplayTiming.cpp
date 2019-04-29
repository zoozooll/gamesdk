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

#include "SwappyVkGoogleDisplayTiming.h"

 /**
 * Concrete/derived class that sits on top of VK_GOOGLE_display_timing
 */
SwappyVkGoogleDisplayTiming::SwappyVkGoogleDisplayTiming(VkPhysicalDevice physicalDevice,
                                                         VkDevice         device,
                                                         SwappyVk         &swappyVk,
                                                         void             *libVulkan) :
    SwappyVkBase(physicalDevice, device, k16_6msec, 1, swappyVk, libVulkan)
{
    initGoogExtension();
    startChoreographerThread();
}

SwappyVkGoogleDisplayTiming::~SwappyVkGoogleDisplayTiming() {
    stopChoreographerThread();
    destroyVkSyncObjects();
}

bool SwappyVkGoogleDisplayTiming::doGetRefreshCycleDuration(VkSwapchainKHR swapchain,
                                                            uint64_t*      pRefreshDuration)  {
    VkRefreshCycleDurationGOOGLE refreshCycleDuration;
    VkResult res = mpfnGetRefreshCycleDurationGOOGLE(mDevice, swapchain, &refreshCycleDuration);
    if (res != VK_SUCCESS) {
        ALOGE("Call to mpfnGetRefreshCycleDurationGOOGLE(...) did not succeed unexpectedly.");
        // This should never occur, but in case it does, return 16,666,666ns:
        mRefreshDur = k16_6msec;
    } else {
        mRefreshDur = refreshCycleDuration.refreshDuration;
    }

    // TEMP CODE: LOG REFRESH DURATION AND RATE:
    double refreshRate = 1000000000.0 / mRefreshDur;
    ALOGD("Returning refresh duration of %" PRIu64 " nsec (approx %f Hz)",
        mRefreshDur, refreshRate);

    *pRefreshDuration = mRefreshDur;
    return true;
}

VkResult SwappyVkGoogleDisplayTiming::initializeVkSyncObjects(VkQueue   queue,
                                                              uint32_t  queueFamilyIndex)
{
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
        VkFenceCreateInfo fence_ci =
                {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .pNext = NULL, .flags = 0};

        res = vkCreateFence(mDevice, &fence_ci, NULL, &sync.fence);
        if (res) {
            ALOGE("failed to create fence: %d", res);
            return res;
        }

        VkSemaphoreCreateInfo semaphore_ci =
                {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = NULL, .flags = 0};

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

        mFreeSync[queue].push_back(sync);
    }

    return VK_SUCCESS;
}

void SwappyVkGoogleDisplayTiming::destroyVkSyncObjects() {
    for (auto it = mPendingSync.begin(); it != mPendingSync.end(); it++) {
        while (mPendingSync[it->first].size() > 0) {
            VkSync sync = mPendingSync[it->first].front();
            mPendingSync[it->first].pop_front();
            vkWaitForFences(mDevice, 1, &sync.fence, VK_TRUE, UINT64_MAX);
            vkResetFences(mDevice, 1, &sync.fence);
            mFreeSync[it->first].push_back(sync);
        }

        while (mFreeSync[it->first].size() > 0) {
            VkSync sync = mFreeSync[it->first].front();
            mFreeSync[it->first].pop_front();
            vkFreeCommandBuffers(mDevice, mCommandPool[it->first], 1, &sync.command);
            vkDestroyEvent(mDevice, sync.event, NULL);
            vkDestroySemaphore(mDevice, sync.semaphore, NULL);
            vkDestroyFence(mDevice, sync.fence, NULL);
        }

        vkDestroyCommandPool(mDevice, mCommandPool[it->first], NULL);
    }
}

void SwappyVkGoogleDisplayTiming::waitForFenceChoreographer(VkQueue queue)
{
    std::unique_lock<std::mutex> lock(mWaitingMutex);
    VkSync sync = mPendingSync[queue].front();
    mPendingSync[queue].pop_front();
    mWaitingCondition.wait(lock, [&]() {
        if (vkWaitForFences(mDevice, 1, &sync.fence, VK_TRUE, 0) == VK_TIMEOUT) {
            postChoreographerCallback();

            // adjust the target frame here as we are waiting additional frame for the fence
            mTargetFrameID++;
            return false;
        }
        return true;
    });

    vkResetFences(mDevice, 1, &sync.fence);
    mFreeSync[queue].push_back(sync);
}

VkResult SwappyVkGoogleDisplayTiming::doQueuePresent(VkQueue                 queue,
                                                     uint32_t                queueFamilyIndex,
                                                     const VkPresentInfoKHR* pPresentInfo)
{
    VkResult ret = initializeVkSyncObjects(queue, queueFamilyIndex);
    if (ret) {
        return ret;
    }

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

    if (mPendingSync[queue].size() >= MAX_PENDING_FENCES) {
        waitForFenceChoreographer(queue);
    }

    // Adjust the presentation time based on the current frameID we are at.
    if(mFrameID < mTargetFrameID) {
        ALOGE("Bad frame ID %ld < target %ld", mFrameID, mTargetFrameID);
        mTargetFrameID = mFrameID;
    }
    mNextDesiredPresentTime += (mFrameID - mTargetFrameID) * mRefreshDur;

    // Setup the new structures to pass:
    VkPresentTimeGOOGLE pPresentTimes[pPresentInfo->swapchainCount];
    for (uint32_t i = 0 ; i < pPresentInfo->swapchainCount ; i++) {
        pPresentTimes[i].presentID = mNextPresentID;
        pPresentTimes[i].desiredPresentTime = mNextDesiredPresentTime;
    }
    mNextPresentID++;

    VkSync sync = mFreeSync[queue].front();
    mFreeSync[queue].pop_front();
    mPendingSync[queue].push_back(sync);

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
    ret = vkQueueSubmit(queue, 1, &submit_info, sync.fence);
    if (ret) {
        ALOGE("Failed to vkQueueSubmit %d", ret);
        return ret;
    }

    VkPresentTimesInfoGOOGLE presentTimesInfo = {VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE,
                                                 pPresentInfo->pNext, pPresentInfo->swapchainCount,
                                                 pPresentTimes};
    VkPresentInfoKHR replacementPresentInfo = {pPresentInfo->sType, &presentTimesInfo,
                                               1,
                                               &sync.semaphore,
                                               pPresentInfo->swapchainCount,
                                               pPresentInfo->pSwapchains,
                                               pPresentInfo->pImageIndices, pPresentInfo->pResults};
    ret = mpfnQueuePresentKHR(queue, &replacementPresentInfo);

    // next present time is going to be 2 intervals from now, leaving 1 interval for cpu work
    // and 1 interval for gpu work
    mNextDesiredPresentTime = mLastframeTimeNanos + 2 * mRefreshDur * mInterval;
    mTargetFrameID = mFrameID + mInterval;

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