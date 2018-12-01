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


#ifndef SWAPPY_EXTERNAL_CHOREOGRAPHER_H
#define SWAPPY_EXTERNAL_CHOREOGRAPHER_H

#include <stdint.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <jni.h>

#ifdef __cplusplus
extern "C" {
#endif

// If app is using choreographer and wants to provide choreographer ticks to swappy,
// call the function below. This function must be called before the first Swappy_swap() call
// for the first time. Afterwards, call this every choreographer tick
void Swappy_onChoreographer(int64_t frameTimeNanos);

#ifdef __cplusplus
};
#endif

#endif //SWAPPY_EXTERNAL_CHOREOGRAPHER_H
