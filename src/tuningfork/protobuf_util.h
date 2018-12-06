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

#pragma once

#include <vector>
#include <cstdint>

#ifdef PROTOBUF_NANO
#include <pb.h>
#endif

namespace tuningfork {

#ifdef PROTOBUF_NANO
struct VectorStream {
    std::vector<uint8_t>* vec;
    size_t it;
    static bool Read(pb_istream_t *stream, uint8_t *buf, size_t count);
    static bool Write(pb_ostream_t *stream, const uint8_t *buf, size_t count);
};
#else
template <typename T>
bool Deserialize(const std::vector<uint8_t> &ser, T &pb) {
    return pb.ParseFromArray(ser.data(), ser.size());
}
template <typename T>
bool Serialize(const T &pb, std::vector<uint8_t> &ser) {
    ser.resize(pb.ByteSize());
    return pb.SerializeToArray(ser.data(), ser.size());
}
template <typename T>
std::vector<uint8_t> Serialize(const T &pb) {
    std::vector<uint8_t> ser(pb.ByteSize());
    pb.SerializeToArray(ser.data(), ser.size());
    return ser;
}
#endif

} // namespace tuningfork {
