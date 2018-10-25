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

#include "Swappy_c.h"
#include "swappy/Swappy.h"
#include "swappy/Settings.h"
#include "swappy/Thread.h"

#include <chrono>

extern "C" void Swappy_init(int64_t refreshPeriodNanos, int64_t appOffsetNanos, int64_t sfOffsetNanos) {
  Swappy::init(std::chrono::nanoseconds(refreshPeriodNanos), std::chrono::nanoseconds(appOffsetNanos), std::chrono::nanoseconds(sfOffsetNanos));
}
extern "C" bool Swappy_swap(EGLDisplay display, EGLSurface surface) {
  return Swappy::swap(display, surface);
}
extern "C" void Swappy_onChoreographer(int64_t frameTimeNanos) {
  Swappy::onChoreographer(frameTimeNanos);
}

extern "C" void Swappy_setPreference(const char* key, const char* value) {
  Settings::getInstance()->setPreference(key,value);
}
uint64_t Swappy_getRefreshPeriodNanos() {
  return Settings::getInstance()->getRefreshPeriod().count();
}

extern "C" bool Swappy_getUseAffinity() {
  return Settings::getInstance()->getUseAffinity();
}

extern "C" int32_t Swappy_getSwapInterval() {
  return Settings::getInstance()->getSwapInterval();
}
