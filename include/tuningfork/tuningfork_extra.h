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

#include "tuningfork.h"

#ifdef __cplusplus
extern "C" {
#endif

// Load settings from assets/tuningfork/tuningfork_settings.bin.
// Returns true and fills 'settings' if the file could be loaded.
// Ownership of settings is passed to the caller: call
//  settings->dealloc(settings->bytes) to avoid a memory leak.
// Returns false if the file was not found or there was an error.
bool TuningFork_findSettingsInAPK(JNIEnv* env, jobject activity,
                                  CProtobufSerialization* settings);

// Load fidelity params from assets/tuningfork/dev_tuningfork_fidelityparams_#.bin.
// Call once with fps=NULL to get the number of files in fp_count.
// The call a second time with a pre-allocated array of size fp_count in fps.
// Ownership of serializations is passed to the caller: call
//  fp->dealloc(fp->bytes) for each one to avoid memory leaks.
void TuningFork_findFidelityParamsInAPK(JNIEnv* env, jobject activity,
                                        CProtobufSerialization* fps,
                                        int* fp_count);

// Initialize tuning fork and automatically inject tracers into Swappy.
// If Swappy is not available or could not be initialized, the function returns
//  false.
// When using Swappy, there will be 2 default tick points added and the
//  histogram settings need to be coordinated with your swap rate.
// If you know the shared library in which Swappy is, pass it in libraryName.
// If libraryName is NULL or TuningFork cannot find Swappy in the library, it
//  will look in the current module and then try in order:
//  [libgamesdk.so, libswappy.so, libunity.so]
bool TuningFork_initWithSwappy(const CProtobufSerialization* settings,
                               JNIEnv* env, jobject activity,
                               const char* libraryName,
                               void (*annotation_callback)());

// This function will be called on a separate thread every time TuningFork
//  performs an upload.
// For internal diagnostic purposes only.
void TuningFork_setUploadCallback(void(*cbk)(const CProtobufSerialization*));

#ifdef __cplusplus
}
#endif
