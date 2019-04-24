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

SwappyVkBase::SwappyVkBase(VkPhysicalDevice physicalDevice,
                           VkDevice         device,
                           uint64_t         refreshDur,
                           uint32_t         interval,
                           SwappyVk         &swappyVk,
                           void             *libVulkan) :
    mPhysicalDevice(physicalDevice), mDevice(device), mRefreshDur(refreshDur),
    mInterval(interval), mSwappyVk(swappyVk), mLibVulkan(libVulkan),
    mInitialized(false)
{
    InitVulkan();

    mpfnGetDeviceProcAddr =
            reinterpret_cast<PFN_vkGetDeviceProcAddr>(
                dlsym(mLibVulkan, "vkGetDeviceProcAddr"));
    mpfnQueuePresentKHR =
            reinterpret_cast<PFN_vkQueuePresentKHR>(
                mpfnGetDeviceProcAddr(mDevice, "vkQueuePresentKHR"));

    mLibAndroid = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
    if (mLibAndroid == nullptr) {
        ALOGE("FATAL: cannot open libandroid.so: %s", strerror(errno));
        abort();
    }

    mAChoreographer_getInstance =
            reinterpret_cast<PFN_AChoreographer_getInstance >(
                dlsym(mLibAndroid, "AChoreographer_getInstance"));

    mAChoreographer_postFrameCallback =
            reinterpret_cast<PFN_AChoreographer_postFrameCallback >(
                    dlsym(mLibAndroid, "AChoreographer_postFrameCallback"));

    mAChoreographer_postFrameCallbackDelayed =
            reinterpret_cast<PFN_AChoreographer_postFrameCallbackDelayed >(
                    dlsym(mLibAndroid, "AChoreographer_postFrameCallbackDelayed"));
    if (!mAChoreographer_getInstance ||
        !mAChoreographer_postFrameCallback ||
        !mAChoreographer_postFrameCallbackDelayed) {
        ALOGE("FATAL: cannot get AChoreographer symbols");
        abort();
    }
}

SwappyVkBase::~SwappyVkBase() {
    if(mLibAndroid)
        dlclose(mLibAndroid);
}

void SwappyVkBase::doSetSwapInterval(VkSwapchainKHR swapchain,
                                     uint32_t       interval) {
    mInterval = interval;
}

void SwappyVkBase::initGoogExtention() {
    mpfnGetRefreshCycleDurationGOOGLE =
            reinterpret_cast<PFN_vkGetRefreshCycleDurationGOOGLE>(
                    mpfnGetDeviceProcAddr(mDevice, "vkGetRefreshCycleDurationGOOGLE"));
    mpfnGetPastPresentationTimingGOOGLE =
            reinterpret_cast<PFN_vkGetPastPresentationTimingGOOGLE>(
                    mpfnGetDeviceProcAddr(mDevice, "vkGetPastPresentationTimingGOOGLE"));
}

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

    mChoreographer = mAChoreographer_getInstance();
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
    struct timespec currTime;
    clock_gettime(CLOCK_MONOTONIC, &currTime);
    uint64_t currentTime =
            ((uint64_t) currTime.tv_sec * kBillion) + (uint64_t) currTime.tv_nsec;

    calcRefreshRate(currentTime);
    mLastframeTimeNanos = currentTime;
    mFrameID++;
    mWaitingCondition.notify_all();

    // queue the next frame callback
    if (mCallbacksBeforeIdle > 0) {
        mCallbacksBeforeIdle--;
        mAChoreographer_postFrameCallbackDelayed(mChoreographer, frameCallback, this, 1);
    }
}

void SwappyVkBase::postChoreographerCallback() {
    if (mCallbacksBeforeIdle == 0) {
        mAChoreographer_postFrameCallbackDelayed(mChoreographer, frameCallback, this, 1);
    }
    mCallbacksBeforeIdle = MAX_CALLBACKS_BEFORE_IDLE;
}

void SwappyVkBase::calcRefreshRate(uint64_t currentTime) {
    long refresh_nano = currentTime - mLastframeTimeNanos;

    if (mRefreshDur != 0 || mLastframeTimeNanos == 0) {
        return;
    }

    mSumRefreshTime += refresh_nano;
    mSamples++;

    if (mSamples == MAX_SAMPLES) {
        mRefreshDur = mSumRefreshTime / mSamples;
    }
}