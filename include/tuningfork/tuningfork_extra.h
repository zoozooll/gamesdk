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

typedef void (*VoidCallback)();
typedef void (*ProtoCallback)(const CProtobufSerialization*);
struct SwappyTracer;
typedef void (*SwappyTracerFn)(const SwappyTracer*);

// Load settings from assets/tuningfork/tuningfork_settings.bin.
// Ownership of settings is passed to the caller: call
//  CProtobufSerialization_Free to deallocate any memory.
// Returns TFERROR_OK and fills 'settings' if the file could be loaded.
// Returns TFERROR_NO_SETTINGS if the file was not found.
TFErrorCode TuningFork_findSettingsInApk(JNIEnv* env, jobject context,
                                         TFSettings* settings);

// Load fidelity params from assets/tuningfork/<filename>
// Ownership of serializations is passed to the caller: call
//  CProtobufSerialization_Free to deallocate any memory.
TFErrorCode TuningFork_findFidelityParamsInApk(JNIEnv* env, jobject context,
                                               const char* filename,
                                               CProtobufSerialization* fp);

// Initialize tuning fork and automatically inject tracers into Swappy.
// There will be at least 2 tick points added.
// Pass a pointer to the Swappy_initTracer function as the 4th argument and
//  the Swappy version number as the 5th.
// frame_callback, if non-NULL, is called once per frame during the Swappy
//  startFrame tracer callback.
TFErrorCode TuningFork_initWithSwappy(const TFSettings* settings,
                                      JNIEnv* env, jobject context,
                                      SwappyTracerFn swappy_tracer_fn,
                                      uint32_t swappy_lib_version,
                                      VoidCallback frame_callback);

// Set a callback to be called on a separate thread every time TuningFork
//  performs an upload.
TFErrorCode TuningFork_setUploadCallback(ProtoCallback cbk);

// This function calls initWithSwappy and also performs the following:
// 1) Settings and default fidelity params are retrieved from the APK.
// 2) A download thread is activated to retrieve fideloty params and retries are
//    performed until a download is successful or a timeout occurs.
// 3) Downloaded params are stored locally and used in preference of default
//    params when the app is started in future.
// fp_default_file_name is the name of the binary fidelity params file that
//  will be used if there is no download connection and there are no saved params.
//  This file must be in assets/tuningfork (but only use the file name here).
// fidelity_params_callback is called with any downloaded params or with default /
//  saved params.
// initialTimeoutMs is the time to wait for an initial download. The fidelity_params_callback
//  will be called after this time with the default / saved params if no params
//  could be downloaded..
// ultimateTimeoutMs is the time after which to stop retrying the download.
TFErrorCode TuningFork_initFromAssetsWithSwappy(JNIEnv* env, jobject context,
                             SwappyTracerFn swappy_tracer_fn,
                             uint32_t swappy_lib_version,
                             VoidCallback frame_callback,
                             const char* fp_default_file_name,
                             ProtoCallback fidelity_params_callback,
                             int initialTimeoutMs, int ultimateTimeoutMs);

// The initFromAssetsWithSwappy function will save fidelity params to a file
//  for use when a download connection is not available. With this function,
//  you can replace or delete any saved file. To delete the file, pass fps=NULL.
TFErrorCode TuningFork_saveOrDeleteFidelityParamsFile(JNIEnv* env, jobject context,
                                                      CProtobufSerialization* fps);

#ifdef __cplusplus
}
#endif
