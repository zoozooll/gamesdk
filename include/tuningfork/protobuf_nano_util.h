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

#include <vector>
#include <cstdint>

#include <pb.h>

namespace tuningfork {

// These structures are useful when serializing and deserializing either vectors of bytes or
//  malloc'd byte streams, using pb_encode and pb_decode.

// VectorStream is a view on the vector provided in vec: it takes no ownership.
// vec must be valid while Read or Write are called. It will be resized as needed by Write.
// Example usage:
//    std::vector<uint8_t> v;
//    VectorStream str {&v, 0};
//    pb_ostream_t stream = {VectorStream::Write, &str, SIZE_MAX, 0};
//    pb_encode(&stream, ...);
struct VectorStream {
    std::vector<uint8_t>* vec;
    size_t it;
    static bool Read(pb_istream_t *stream, uint8_t *buf, size_t count);
    static bool Write(pb_ostream_t *stream, const uint8_t *buf, size_t count);
};

// ByteStream is a view on the bytes provided in vec. Write will call realloc
//  if more bytes are needed and it is up to the caller to free the data allocated.
// It is valid to set vec=nullptr and size=0, in which case vec will be allocated using
//  malloc.
struct ByteStream {
    uint8_t* vec;
    size_t size;
    size_t it;
    static bool Read(pb_istream_t *stream, uint8_t *buf, size_t count);
    static bool Write(pb_ostream_t *stream, const uint8_t *buf, size_t count);
};

} // namespace tuningfork {
