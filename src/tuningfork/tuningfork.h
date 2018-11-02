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

#ifndef TUNINGFORK_TUNINGFORK_H
#define TUNINGFORK_TUNINGFORK_H

#include <stdint.h>
#include <string>
#include <chrono>
#include <vector>

namespace tuningfork {

// These are reserved instrumentation keys
enum {
    TFTICK_SYSCPU = 0,
    TFTICK_SYSGPU = 1
};

typedef std::vector<uint8_t> ProtobufSerialization;

template <typename T>
bool SerializationToProtobuf(const ProtobufSerialization &ser, T &pb) {
    return pb.ParseFromArray(ser.data(), ser.size());
}
template <typename T>
bool ProtobufToSerialization(const T &pb, ProtobufSerialization &ser) {
    ser.resize(pb.ByteSize());
    return pb.SerializeToArray(ser.data(), ser.size());
}
template <typename T>
ProtobufSerialization Serialize(const T &pb) {
    ProtobufSerialization ser(pb.ByteSize());
    pb.SerializeToArray(ser.data(), ser.size());
    return ser;
}

// The instrumentation key identifies a tick point within a frame or a trace segment
typedef uint16_t InstrumentationKey;
typedef uint64_t TraceHandle;
typedef std::chrono::steady_clock::time_point TimePoint;
typedef std::chrono::steady_clock::duration Duration;

class Backend {
public:
    virtual ~Backend() {};
    virtual bool GetFidelityParams(ProtobufSerialization &fidelity_params, size_t timeout_ms) = 0;
    virtual bool Process(const ProtobufSerialization &tuningfork_log_event) = 0;
};

class DebugBackend : public Backend {
public:
    ~DebugBackend() override;
    bool GetFidelityParams(ProtobufSerialization &fidelity_params, size_t timeout_ms) override;
    bool Process(const ProtobufSerialization &tuningfork_log_event) override;
};

// You can provide your own time source rather than steady_clock by inheriting this and passing
//   it to init.
class ITimeProvider {
public:
    virtual std::chrono::steady_clock::time_point NowNs() = 0;
};

// init must be called before any other functions
//  If no backend is passed, a debug version is used which returns empty fidelity params
//   and outputs histograms in protobuf text format to logcat.
//  If no timeProvider is passed, std::chrono::steady_clock is used.
void Init(const ProtobufSerialization &settings, Backend *backend = 0,
          ITimeProvider *time_provider = 0);

// Blocking call to get fidelity parameters from the server.
// Returns true if parameters could be downloaded within the timeout, false otherwise.
// Note that once fidelity parameters are downloaded, any timing information is recorded
//  as being associated with those parameters.
bool GetFidelityParameters(ProtobufSerialization &params, size_t timeout_ms);

// Protobuf serialization of the current annotation
void SetCurrentAnnotation(const ProtobufSerialization &annotation);

// Record a frame tick that will be associated with the instrumentation key and the current
//   annotation
void FrameTick(InstrumentationKey id);

// Record a frame tick using an external time, rather than system time
void FrameDeltaTimeNanos(InstrumentationKey id, Duration dt);

// Start a trace segment
TraceHandle StartTrace(InstrumentationKey key);

// Record a trace with the key and annotation set using startTrace
void EndTrace(TraceHandle h);

} // namespace tuningfork {

#endif
