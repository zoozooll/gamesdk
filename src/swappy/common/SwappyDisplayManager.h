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

#pragma once

#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>


namespace swappy {

class SwappyDisplayManager {
public:
    SwappyDisplayManager(JavaVM*, jobject);
    ~SwappyDisplayManager();

    bool isInitialized() { return mInitialized; }

    using RefreshRateMap = std::map<std::chrono::nanoseconds, int>;

    std::shared_ptr<RefreshRateMap> getSupportedRefreshRates();

    void setPreferredRefreshRate(int index);

private:
    JavaVM* mJVM;
    std::mutex mMutex;
    std::condition_variable mCondition;
    std::shared_ptr<RefreshRateMap> mSupportedRefreshRates;
    jobject mJthis;
    jmethodID mSetPreferredRefreshRate;
    bool mInitialized = false;

    friend class SwappyDisplayManagerJNI;
};

} // namespace swappy