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

// OpenGL part of swappy

#pragma once

#include "swappy_common.h"

#include <stdint.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <jni.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize Swappy, getting the required Android parameters from the display subsystem via JNI
void SwappyGL_init(JNIEnv *env, jobject jactivity);

// Returns true if Swappy was successfully initialized.
// Returns false if either the 'swappy.disable' system property is not 'false'
//  or the required OpenGL extensions are not available for Swappy to work.
bool SwappyGL_isEnabled();

// Destroy resources and stop all threads that swappy has created
void SwappyGL_destroy();

// Replace calls to eglSwapBuffers with this. Swappy will wait for the previous frame's
// buffer to be processed by the GPU before actually calling eglSwapBuffers.
bool SwappyGL_swap(EGLDisplay display, EGLSurface surface);

// Parameter setters
void SwappyGL_setRefreshPeriod(uint64_t period_ns);
void SwappyGL_setUseAffinity(bool tf);
void SwappyGL_setSwapIntervalNS(uint64_t swap_ns);

// Parameter getters
uint64_t SwappyGL_getRefreshPeriodNanos();
uint64_t SwappyGL_getSwapIntervalNS();
bool SwappyGL_getUseAffinity();

#ifdef __cplusplus
};
#endif
