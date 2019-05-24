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

#include "tuningfork/tuningfork.h"
#include "tuningfork/tuningfork_extra.h"
#include "tuningfork/protobuf_util.h"
#include "tuningfork_internal.h"
#include <jni.h>

#include <cstdlib>
#include <cstring>

namespace {

tuningfork::ProtobufSerialization ToProtobufSerialization(const CProtobufSerialization& cpbs) {
    return tuningfork::ProtobufSerialization(cpbs.bytes, cpbs.bytes + cpbs.size);
}
void ToCProtobufSerialization(const tuningfork::ProtobufSerialization& pbs,
                              CProtobufSerialization* cpbs) {
    cpbs->bytes = (uint8_t*)::malloc(pbs.size());
    memcpy(cpbs->bytes, pbs.data(), pbs.size());
    cpbs->size = pbs.size();
    cpbs->dealloc = CProtobufSerialization_Dealloc;
}

} // anonymous namespace

extern "C" {

TFErrorCode TuningFork_init_internal(const TFSettings *settings, JNIEnv* env, jobject context) {
    if (settings) {
        return tuningfork::Init(*settings, env, context);
    } else {
        TFSettings apk_settings;
        if (TuningFork_findSettingsInApk(env, context, &apk_settings)==TFERROR_OK)
            return tuningfork::Init(apk_settings, env, context);
        else
            return TFERROR_NO_SETTINGS;
    }
}

// Blocking call to get fidelity parameters from the server.
// Note that once fidelity parameters are downloaded, any timing information is recorded
//  as being associated with those parameters.
TFErrorCode TuningFork_getFidelityParameters(JNIEnv* env, jobject context,
                                      const char* url_base,
                                      const char* api_key,
                                      const CProtobufSerialization *defaultParams,
                                      CProtobufSerialization *params, uint32_t timeout_ms) {
    tuningfork::ProtobufSerialization defaults;
    if(defaultParams)
        defaults = ToProtobufSerialization(*defaultParams);
    tuningfork::ProtobufSerialization s;
    TFErrorCode result = tuningfork::GetFidelityParameters(env, context, url_base,
                                                           api_key?api_key:"",
                                                           defaults, s, timeout_ms);
    if (result==TFERROR_OK && params)
        ToCProtobufSerialization(s, params);
    return result;
}

// Protobuf serialization of the current annotation
TFErrorCode TuningFork_setCurrentAnnotation(const CProtobufSerialization *annotation) {
    if(annotation)
        return tuningfork::SetCurrentAnnotation(ToProtobufSerialization(*annotation));
    else
        return TFERROR_INVALID_ANNOTATION;
}

// Record a frame tick that will be associated with the instrumentation key and the current
//   annotation
TFErrorCode TuningFork_frameTick(TFInstrumentKey id) {
    return tuningfork::FrameTick(id);
}

// Record a frame tick using an external time, rather than system time
TFErrorCode TuningFork_frameDeltaTimeNanos(TFInstrumentKey id, TFDuration dt) {
    return tuningfork::FrameDeltaTimeNanos(id, std::chrono::nanoseconds(dt));
}

// Start a trace segment
TFErrorCode  TuningFork_startTrace(TFInstrumentKey key, TFTraceHandle* handle) {
    if (handle==nullptr) return TFERROR_INVALID_TRACE_HANDLE;
    return tuningfork::StartTrace(key, *handle);
}

// Record a trace with the key and annotation set using startTrace
TFErrorCode TuningFork_endTrace(TFTraceHandle h) {
    return tuningfork::EndTrace(h);
}

TFErrorCode TuningFork_flush() {
    return tuningfork::Flush();
}

void TUNINGFORK_VERSION_SYMBOL() {
    // Intentionally empty: this function is used to ensure that the proper
    // version of the library is linked against the proper headers.
    // In case of mismatch, a linker error will be triggered because of an
    // undefined symbol, as the name of the function depends on the version.
}

} // extern "C" {
