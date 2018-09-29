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

#include <thread>
#include <vector>

class ChoreographerFilter {
  public:
    explicit ChoreographerFilter(std::chrono::nanoseconds refreshPeriod,
                                 std::chrono::nanoseconds appToSfDelay,
                                 std::function<void()> doWork);
    ~ChoreographerFilter();

    void onChoreographer();

  private:
    void launchThreadsLocked();
    void terminateThreadsLocked();

    void onSettingsChanged();

    void threadMain(bool useAffinity, int32_t thread);

    std::mutex mThreadPoolMutex;
    bool mUseAffinity = true;
    std::vector<std::thread> mThreadPool;

    std::mutex mMutex;
    std::condition_variable mCondition;
    bool mIsRunning = true;
    int64_t mSequenceNumber = 0;
    std::chrono::steady_clock::time_point mLastTimestamp;

    std::mutex mWorkMutex;
    std::chrono::steady_clock::time_point mLastWorkRun;

    const std::chrono::nanoseconds mRefreshPeriod;
    const std::chrono::nanoseconds mAppToSfDelay;
    const std::function<void()> mDoWork;
};