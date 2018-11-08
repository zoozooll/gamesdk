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

#include "Settings.h"

#define LOG_TAG "Settings"

#include <memory>

#include "Log.h"

Settings *Settings::getInstance() {
    static auto settings = std::make_unique<Settings>(ConstructorTag{});
    return settings.get();
}

void Settings::addListener(Listener listener) {
    std::lock_guard lock(mMutex);
    mListeners.emplace_back(std::move(listener));
}

void Settings::setPreference(std::string key, std::string value) {
    {
        std::lock_guard lock(mMutex);
        if (key == "refresh_period") {
            mRefreshPeriod = std::chrono::nanoseconds{std::stoll(value)};
        } else if (key == "swap_interval") {
            mSwapInterval = std::stoi(value);
        } else if (key == "use_affinity") {
            mUseAffinity = (value == "true");
        } else if (key == "hot_pocket") {
            mHotPocket = (value == "true");
        } else {
            ALOGI("Can't find matching preference for %s", key.c_str());
            return;
        }
    }

    // Notify the listeners without the lock held
    notifyListeners();
}

void Settings::setRefreshPeriod(std::chrono::nanoseconds period) {
    {
        std::lock_guard lock(mMutex);
        mRefreshPeriod = period;
    }
    // Notify the listeners without the lock held
    notifyListeners();
}
void Settings::setSwapInterval(uint32_t num_frames) {
    {
        std::lock_guard lock(mMutex);
        mSwapInterval = num_frames;
    }
    // Notify the listeners without the lock held
    notifyListeners();
}
void Settings::setUseAffinity(bool tf) {
    {
        std::lock_guard lock(mMutex);
        mUseAffinity = tf;
    }
    // Notify the listeners without the lock held
    notifyListeners();
}


std::chrono::nanoseconds Settings::getRefreshPeriod() const {
    std::lock_guard lock(mMutex);
    return mRefreshPeriod;
}

int32_t Settings::getSwapInterval() const {
    std::lock_guard lock(mMutex);
    return mSwapInterval;
}

bool Settings::getUseAffinity() const {
    std::lock_guard lock(mMutex);
    return mUseAffinity;
}

bool Settings::getHotPocket() const {
    std::lock_guard lock(mMutex);
    return mHotPocket;
}

void Settings::notifyListeners() {
    // Grab a local copy of the listeners
    std::vector<Listener> listeners;
    {
        std::lock_guard lock(mMutex);
        listeners = mListeners;
    }

    // Call the listeners without the lock held
    for (const auto &listener : listeners) {
        listener();
    }
}