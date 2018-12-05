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

#include <stdint.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <jni.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize Swappy, getting the required Android parameters from the display subsystem via JNI
void Swappy_init(JNIEnv *env, jobject jactivity);

// Destroy resources and stop all threads that swappy has created
void Swappy_destroy();

// Replace calls to eglSwapBuffers with this. Swappy will wait for the previous frame's
// buffer to be processed by the GPU before actually calling eglSwapBuffers.
bool Swappy_swap(EGLDisplay display, EGLSurface surface);

// Parameter setters
void Swappy_setRefreshPeriod(uint64_t period_ns);
void Swappy_setUseAffinity(bool tf);
void Swappy_setSwapInterval(uint32_t num_frames);

// Parameter getters
uint64_t Swappy_getRefreshPeriodNanos();
int32_t Swappy_getSwapInterval();
bool Swappy_getUseAffinity();

#ifdef __cplusplus
};
#endif
