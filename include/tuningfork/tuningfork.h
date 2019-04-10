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
#include <jni.h>

#define TUNINGFORK_MAJOR_VERSION 0
#define TUNINGFORK_MINOR_VERSION 1
#define TUNINGFORK_PACKED_VERSION ((TUNINGFORK_MAJOR_VERSION<<16)|(TUNINGFORK_MINOR_VERSION))

// Instrument keys 64000-65535 are reserved
enum {
    TFTICK_USERDEFINED_BASE = 0,
    TFTICK_SYSCPU = 64000,
    TFTICK_SYSGPU = 64001,
    TFTICK_SWAPPY_WAIT_TIME = 64002,
    TFTICK_SWAPPY_SWAP_TIME = 64003
};

struct CProtobufSerialization {
    uint8_t* bytes;
    size_t size;
    void (*dealloc)(struct CProtobufSerialization*);
};

// The instrumentation key identifies a tick point within a frame or a trace segment
typedef uint16_t TFInstrumentKey;
typedef uint64_t TFTraceHandle;
typedef uint64_t TFTimePoint;
typedef uint64_t TFDuration;

enum TFErrorCode {
    TFERROR_OK = 0, // No error
    TFERROR_NO_SETTINGS = 1, // No tuningfork_settings.bin found in assets/tuningfork.
    TFERROR_NO_SWAPPY = 2, // Not able to find Swappy.
    TFERROR_INVALID_DEFAULT_FIDELITY_PARAMS = 3, // fpDefaultFileNum is out of range.
    TFERROR_NO_FIDELITY_PARAMS = 4, // No dev_tuningfork_fidelityparams_#.bin found
                                   //  in assets/tuningfork.
    TFERROR_TUNINGFORK_NOT_INITIALIZED = 5,
    TFERROR_INVALID_ANNOTATION = 6,
    TFERROR_INVALID_INSTRUMENT_KEY = 7,
    TFERROR_INVALID_TRACE_HANDLE = 8,
    TFERROR_TIMEOUT = 9,
    TFERROR_BAD_PARAMETER = 10,
    TFERROR_COULDNT_SAVE_OR_DELETE_FPS = 11
};

struct TFHistogram {
    int32_t instrument_key;
    float bucket_min;
    float bucket_max;
    int32_t n_buckets;
};
struct TFAggregationStrategy {
    enum TFSubmissionPolicy {
      TIME_BASED = 1,
      TICK_BASED = 2
    };
    TFSubmissionPolicy method;
    uint32_t intervalms_or_count;
    uint32_t max_instrumentation_keys;
    uint32_t n_annotation_enum_size;
    uint32_t* annotation_enum_size;
};
struct TFSettings {
  TFAggregationStrategy aggregation_strategy;
  uint32_t n_histograms;
  TFHistogram* histograms;
  void (*dealloc)(TFSettings*);
};

#ifdef __cplusplus
extern "C" {
#endif

inline void CProtobufSerialization_Free(CProtobufSerialization* ser) {
    if(ser->dealloc) ser->dealloc(ser);
}
inline void TFSettings_Free(TFSettings* settings) {
    if(settings->dealloc) settings->dealloc(settings);
}

// TuningFork_init must be called before any other functions.
// If settings is a null pointer, settings are extracted from the app.
// Returns TFERROR_OK if successful, TFERROR_NO_SETTINGS if no settings could be found.
TFErrorCode TuningFork_init(const TFSettings *settings, JNIEnv* env, jobject context);

// Blocking call to get fidelity parameters from the server.
// Note that once fidelity parameters are downloaded, any timing information is recorded
//  as being associated with those parameters.
// If you subsequently call GetFidelityParameters and a new set of parameters is downloaded,
// any data that is already collected will be submitted to the backend.
// Ownership of 'params' is transferred to the caller, so they must call params->dealloc
// when they are done with it.
// Returns TFERROR_TIMEOUT if there was a timeout before params could be downloaded.
// Returns TFERROR_OK on success.
TFErrorCode TuningFork_getFidelityParameters(const CProtobufSerialization *defaultParams,
                             CProtobufSerialization *params, uint32_t timeout_ms);

// Protobuf serialization of the current annotation.
// Returns TFERROR_INVALID_ANNOTATION if annotation is inconsistent with the settings.
// Returns TFERROR_OK on success.
TFErrorCode TuningFork_setCurrentAnnotation(const CProtobufSerialization *annotation);

// Record a frame tick that will be associated with the instrumentation key and the current
//   annotation.
// NB: calling the tick or trace functions from different threads is allowed, but a given
//  instrument key should always be ticked from the same thread.
// Returns TFERROR_INVALID_INSTRUMENT_KEY if the instrument key is invalid.
// Returns TFERROR_OK on success.
TFErrorCode TuningFork_frameTick(TFInstrumentKey id);

// Record a frame tick using an external time, rather than system time.
// Returns TFERROR_INVALID_INSTRUMENT_KEY if the instrument key is invalid.
// Returns TFERROR_OK on success.
TFErrorCode TuningFork_frameDeltaTimeNanos(TFInstrumentKey id, TFDuration dt);

// Start a trace segment.
// handle is filled with a new handle on success.
// Returns TFERROR_INVALID_INSTRUMENT_KEY if the instrument key is invalid.
// Returns TFERROR_OK on success.
TFErrorCode TuningFork_startTrace(TFInstrumentKey key, TFTraceHandle* handle);

// Record a trace with the key and annotation set using startTrace.
// Returns TFERROR_INVALID_TRACE_HANDLE if the handle is invalid.
// Returns TFERROR_OK on success.
TFErrorCode TuningFork_endTrace(TFTraceHandle h);

#ifdef __cplusplus
}
#endif
