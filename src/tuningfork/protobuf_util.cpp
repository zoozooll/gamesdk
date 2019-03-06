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

#include "protobuf_util.h"

#ifdef PROTOBUF_NANO
#include <pb_encode.h>
#include <pb_decode.h>
#endif

namespace tuningfork {

#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte)  \
  (byte & 0x80 ? '1' : '0'), \
  (byte & 0x40 ? '1' : '0'), \
  (byte & 0x20 ? '1' : '0'), \
  (byte & 0x10 ? '1' : '0'), \
  (byte & 0x08 ? '1' : '0'), \
  (byte & 0x04 ? '1' : '0'), \
  (byte & 0x02 ? '1' : '0'), \
  (byte & 0x01 ? '1' : '0')

#ifdef PROTOBUF_NANO
bool VectorStream::Read(pb_istream_t *stream, uint8_t *buf, size_t count) {
    VectorStream* str = (VectorStream*)(stream->state);
    if (buf==NULL) {
        if(count > str->vec->size() - str->it) {
            str->it = str->vec->size();
            return false;
        }
        else {
            str->it += count;
            return true;
        }
    }
    auto p = &(*str->vec)[str->it];
    auto n = std::min(count, str->vec->size() - str->it);
    std::copy(p, p + n, buf);
    str->it += n;
    return n==count;
}
bool VectorStream::Write(pb_ostream_t *stream, const uint8_t *buf, size_t count) {
    if(buf==NULL)
        return true;
    VectorStream* str = (VectorStream*)(stream->state);
    auto vec = str->vec;
    int b = buf[0];
    auto sz = vec->size();
    vec->resize(sz+count);
    std::copy(buf, buf+count, &(*vec)[sz]);
    return true;
}
#endif

} // namespace tuningfork {
