/*
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

#include "tuningfork_c.h"
#include "tuningfork.h"

namespace {
tuningfork::ProtobufSerialization ToProtobufSerialization(const CProtobufSerialization& cpbs) {
  return tuningfork::ProtobufSerialization(cpbs.bytes, cpbs.bytes + cpbs.size);
}
void ToCProtobufSerialization(const tuningfork::ProtobufSerialization& pbs, CProtobufSerialization* cpbs) {
  cpbs->bytes = (uint8_t*)::malloc(pbs.size());
  cpbs->size = pbs.size();
  cpbs->dealloc = ::free;
}
}

extern "C" {

// init must be called before any other functions
//  If no backend is passed, a debug version is used which returns empty fidelity params
//   and outputs histograms in protobuf text format to logcat.
//  If no timeProvider is passed, std::chrono::steady_clock is used.
void TFInit(const CProtobufSerialization *settings) {
  if(settings)
    tuningfork::Init(ToProtobufSerialization(*settings));
}

// Blocking call to get fidelity parameters from the server.
// Returns true if parameters could be downloaded within the timeout, false otherwise.
// Note that once fidelity parameters are downloaded, any timing information is recorded
//  as being associated with those parameters.
bool TFGetFidelityParameters(CProtobufSerialization *params, size_t timeout_ms) {
  tuningfork::ProtobufSerialization s;
  bool result = tuningfork::GetFidelityParameters(s, timeout_ms);
  if(params)
    ToCProtobufSerialization(s, params);
  return result;
}

// Protobuf serialization of the current annotation
void TFSetCurrentAnnotation(const CProtobufSerialization *annotation) {
  if(annotation)
    tuningfork::SetCurrentAnnotation(ToProtobufSerialization(*annotation));
}

// Record a frame tick that will be associated with the instrumentation key and the current
//   annotation
void TFFrameTick(InstrumentationKey id) {
  tuningfork::FrameTick(id);
}

// Record a frame tick using an external time, rather than system time
void TFFrameDeltaTimeNanos(InstrumentationKey id, Duration dt) {
  tuningfork::FrameDeltaTimeNanos(id, std::chrono::nanoseconds(dt));
}

// Start a trace segment
TraceHandle TFStartTrace(InstrumentationKey key) {
  return tuningfork::StartTrace(key);
}

// Record a trace with the key and annotation set using startTrace
void TFEndTrace(TraceHandle h) {
  tuningfork::EndTrace(h);
}

} // extern "C" {
