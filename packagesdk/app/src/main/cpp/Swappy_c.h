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


#ifndef SWAPPY_SWAPPY_C_H
#define SWAPPY_SWAPPY_C_H

#include <stdint.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#ifdef __cplusplus
extern "C" {
#endif

void Swappy_init(int64_t refreshPeriodNanos, int64_t appOffsetNanos, int64_t sfOffsetNanos);
bool Swappy_swap(EGLDisplay display, EGLSurface surface);
void Swappy_onChoreographer(int64_t frameTimeNanos);

void Swappy_setPreference(const char* name, const char* value);
uint64_t Swappy_getRefreshPeriodNanos();

int32_t Swappy_getSwapInterval();

bool Swappy_getUseAffinity();

#ifdef __cplusplus
};
#endif

#endif //SWAPPY_SWAPPY_C_H
