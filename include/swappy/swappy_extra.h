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

// Pass callbacks to be called each frame to trace execution
struct SwappyTracer {
    void (*preWait)(void*);
    void (*postWait)(void*);
    void (*preSwapBuffers)(void*);
    void (*postSwapBuffers)(void*);
    void (*startFrame)(void*);
    void* userData;
};
void Swappy_injectTracer(const SwappyTracer *t);

#ifdef __cplusplus
};
#endif
