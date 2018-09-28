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

#include "ChoreographerFilter.h"

#define LOG_TAG "ChoreographerFilter"

#include <sched.h>
#include <unistd.h>

#include <deque>
#include <string>

#include "swappy-utils/Log.h"
#include "swappy-utils/Settings.h"
#include "swappy-utils/Thread.h"
#include "swappy-utils/Trace.h"

using namespace std::chrono_literals;
using time_point = std::chrono::steady_clock::time_point;

namespace {
class Timer {
  public:
    Timer(std::chrono::nanoseconds refreshPeriod, std::chrono::nanoseconds appToSfDelay)
        : mRefreshPeriod(refreshPeriod),
          mAppToSfDelay(appToSfDelay) {}

    void addTimestamp(time_point point) {
        point -= mAppToSfDelay;

        while (mBaseTime + mRefreshPeriod * 1.5 < point) {
            mBaseTime += mRefreshPeriod;
        }

        std::chrono::nanoseconds delta = (point - (mBaseTime + mRefreshPeriod));
        if (delta < -mRefreshPeriod / 2 || delta > mRefreshPeriod / 2) {
            return;
        }

        // TODO: 0.2 weighting factor for exponential smoothing is completely arbitrary
        mBaseTime += mRefreshPeriod + delta * 2 / 10;
    }

    void sleep() {
        const auto now = std::chrono::steady_clock::now();
        auto targetTime = mBaseTime + mRefreshPeriod;
        while (targetTime < now) {
            targetTime += mRefreshPeriod;
        }

        std::this_thread::sleep_until(targetTime);
    }

  private:
    const std::chrono::nanoseconds mRefreshPeriod;
    const std::chrono::nanoseconds mAppToSfDelay;
    time_point mBaseTime = std::chrono::steady_clock::now();
};
}

ChoreographerFilter::ChoreographerFilter(std::chrono::nanoseconds refreshPeriod,
                                         std::chrono::nanoseconds appToSfDelay,
                                         std::function<void()> doWork)
    : mRefreshPeriod(refreshPeriod),
      mAppToSfDelay(appToSfDelay),
      mDoWork(doWork) {
    Settings::getInstance()->addListener([this]() { onSettingsChanged(); });

    std::lock_guard lock(mThreadPoolMutex);
    mUseAffinity = Settings::getInstance()->getUseAffinity();
    launchThreadsLocked();
}

ChoreographerFilter::~ChoreographerFilter() {
    std::lock_guard lock(mThreadPoolMutex);
    terminateThreadsLocked();
}

void ChoreographerFilter::onChoreographer() {
    std::unique_lock lock(mMutex);
    mLastTimestamp = std::chrono::steady_clock::now();
    ++mSequenceNumber;
    mCondition.notify_all();
}

void ChoreographerFilter::launchThreadsLocked() {
    {
        std::lock_guard lock(mMutex);
        mIsRunning = true;
    }

    const int32_t numThreads = getNumCpus() > 2 ? 2 : 1;
    for (int32_t thread = 0; thread < numThreads; ++thread) {
        mThreadPool.push_back(std::thread([this, thread]() { threadMain(mUseAffinity, thread); }));
    }
}

void ChoreographerFilter::terminateThreadsLocked() {
    {
        std::lock_guard lock(mMutex);
        mIsRunning = false;
        mCondition.notify_all();
    }

    for (auto &thread : mThreadPool) {
        thread.join();
    }
    mThreadPool.clear();
}

void ChoreographerFilter::onSettingsChanged() {
    const bool useAffinity = Settings::getInstance()->getUseAffinity();
    std::lock_guard lock(mThreadPoolMutex);
    if (useAffinity == mUseAffinity) {
        return;
    }

    terminateThreadsLocked();
    mUseAffinity = useAffinity;
    launchThreadsLocked();
}

void ChoreographerFilter::threadMain(bool useAffinity, int32_t thread) {
    Timer timer(mRefreshPeriod, mAppToSfDelay);

    if (useAffinity) {
        ALOGI("Using affinity");

        // Set filter threads to run on the last CPU(s)
        setAffinity(getNumCpus() - 1 - thread);
    }

    std::string threadName = "Filter";
    threadName += std::to_string(thread);
    pthread_setname_np(pthread_self(), threadName.c_str());

    std::unique_lock lock(mMutex);
    while (mIsRunning) {
        auto timestamp = mLastTimestamp;
        lock.unlock();
        timer.addTimestamp(timestamp);
        timer.sleep();
        {
            std::unique_lock workLock(mWorkMutex);
            const auto now = std::chrono::steady_clock::now();
            if (now - mLastWorkRun > mRefreshPeriod / 2) {
                // Assume we got here first and there's work to do
                ScopedTrace trace("doWork");
                mDoWork();
                mLastWorkRun = now;
            }
        }
        lock.lock();
    }
}