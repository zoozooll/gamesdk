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

#include "uploadthread.h"
#include "prong.h"
#include "histogram.h"

#include "pb_encode.h"

#include "nano/tuningfork.pb.h"
#include "nano/tuningfork_clearcut_log.pb.h"

namespace tuningfork {

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
    // Fill in the experiment ID
    static void FillExperimentID(const std::string& experiment_id, TuningForkLogEvent& evt);

    // Callbacks needed by nanopb
    static bool writeCountArray(pb_ostream_t *stream, const pb_field_t *field, void *const *arg);
    static bool writeAnnotation(pb_ostream_t* stream, const pb_field_t *field, void *const *arg);
    static bool writeHistograms(pb_ostream_t* stream, const pb_field_t *field, void *const *arg);
    static bool writeFidelityParams(pb_ostream_t* stream, const pb_field_t *field, void *const *arg);
    static bool writeExperimentId(pb_ostream_t *stream, const pb_field_t *field, void *const *arg);
};

} //namespace tuningfork
