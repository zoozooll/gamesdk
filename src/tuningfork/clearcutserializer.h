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

#include "uploadthread.h"
#include "prong.h"
#include "histogram.h"

#include "pb_encode.h"

#include "nano/tuningfork.pb.h"
#include "nano/tuningfork_clearcut_log.pb.h"

namespace tuningfork {

typedef com_google_tuningfork_FidelityParams FidelityParams;
typedef com_google_tuningfork_Annotation Annotation;
typedef logs_proto_tuningfork_TuningForkLogEvent TuningForkLogEvent;

typedef logs_proto_tuningfork_TuningForkHistogram ClearcutHistogram;

class ClearcutSerializer {
public:
    static void SerializeEvent(const ProngCache& t,
                               const ProtobufSerialization& fidelity_params,
                               ProtobufSerialization& evt_ser);
    // Fill in the event histograms
    static void FillHistograms(const ProngCache& pc, TuningForkLogEvent &evt);
    // Fill in the annotation, etc, then the histogram
    static void Fill(const Prong& p, ClearcutHistogram& h);
    // Fill in the histogram data
    static void Fill(const Histogram& h, ClearcutHistogram& ch);

    // Callbacks needed by nanopb
    static bool writeCountArray(pb_ostream_t *stream, const pb_field_t *field, void *const *arg);
    static bool writeAnnotation(pb_ostream_t* stream, const pb_extension_t *extension);
    static bool writeHistograms(pb_ostream_t* stream, const pb_field_t *field, void *const *arg);
    static bool writeFidelityParams(pb_ostream_t* stream, const pb_extension_t *extension);
    // Used by annotation serializer
    static pb_extension_t ext_;
    static pb_extension_type_t ext_type_;
    // Used by fidelity params serializer
    static pb_extension_t ext2_;
    static pb_extension_type_t ext2_type_;

};

} //namespace tuningfork {
