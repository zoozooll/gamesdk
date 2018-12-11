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

#include "clearcutserializer.h"

#include "protobuf_util.h"

namespace tuningfork {

pb_extension_t ClearcutSerializer::ext_;
pb_extension_type_t ClearcutSerializer::ext_type_;
pb_extension_t ClearcutSerializer::ext2_;
pb_extension_type_t ClearcutSerializer::ext2_type_;

bool ClearcutSerializer::writeCountArray(pb_ostream_t *stream, const pb_field_t *field,
                                       void *const *arg) {
    const Histogram* h = static_cast<Histogram*>(*arg);
    if(!pb_encode_tag(stream, PB_WT_STRING, logs_proto_tuningfork_TuningForkHistogram_counts_tag))
        return false;
    // Get the length of the data
    pb_ostream_t sizing_stream = PB_OSTREAM_SIZING;
    for (int i = 0; i < h->num_buckets_; ++i)
        pb_encode_varint(&sizing_stream, h->buckets_[i]);
    // Encode the length of the packed array in bytes
    if (!pb_encode_varint(stream, sizing_stream.bytes_written))
        return false;
    // Encode each item, without the type, since it's packed
    for (int i = 0; i < h->num_buckets_; ++i) {
        if(!pb_encode_varint(stream, h->buckets_[i]))
            return false;
    }
    return true;
}

void ClearcutSerializer::Fill(const Histogram& h, ClearcutHistogram& ch) {
     ch.counts.funcs.encode = writeCountArray;
     ch.counts.arg = (void*)&h;
}

bool ClearcutSerializer::writeAnnotation(pb_ostream_t* stream, const pb_extension_t *extension) {
    const Prong* p = static_cast<const Prong*>(extension->type->arg);
    if(p->annotation_.size()>0) {
        pb_write(stream, &p->annotation_[0], p->annotation_.size());
    }
    return true;
}
void ClearcutSerializer::Fill(const Prong& p, ClearcutHistogram& h) {
    h.has_instrument_id = true;
    h.instrument_id = p.instrumentation_key_;
    h.has_annotation = true;
    h.annotation.extensions = &ext_;
    ext_.type = &ext_type_;
    ext_.next = 0;
    ext_type_.encode = writeAnnotation;
    ext_type_.arg = &p;
    Fill(p.histogram_, h);
}
bool ClearcutSerializer::writeHistograms(pb_ostream_t* stream, const pb_field_t *field,
                                         void *const *arg) {
    const ProngCache* pc =static_cast<const ProngCache*>(*arg);
    for (auto &p: pc->prongs_) {
        if (p->histogram_.Count() > 0) {
            ClearcutHistogram h;
            Fill(*p, h);
            pb_encode_tag_for_field(stream, field);
            // Get size, then fill object
            pb_ostream_t sizing_stream = PB_OSTREAM_SIZING;
            pb_encode(&sizing_stream, logs_proto_tuningfork_TuningForkHistogram_fields, &h);
            pb_encode_varint(stream, sizing_stream.bytes_written);
            pb_encode(stream, logs_proto_tuningfork_TuningForkHistogram_fields, &h);
        }
    }
    return true;
}

void ClearcutSerializer::FillHistograms(const ProngCache& pc, TuningForkLogEvent &evt) {
    evt.histograms.funcs.encode = writeHistograms;
    evt.histograms.arg = (void*)&pc;
}

bool ClearcutSerializer::writeFidelityParams(pb_ostream_t* stream, const pb_extension_t *extension) {
    const ProtobufSerialization* fp = static_cast<const ProtobufSerialization*>(extension->type->arg);
    if(fp->size()>0) {
      pb_write(stream, &(*fp)[0], fp->size());
    }
    return true;
}
void ClearcutSerializer::SerializeEvent(const ProngCache& pc,
                                        const ProtobufSerialization& fidelity_params,
                                        ProtobufSerialization& evt_ser) {
    TuningForkLogEvent evt = logs_proto_tuningfork_TuningForkLogEvent_init_default;
    evt.has_fidelityparams = true;
    evt.fidelityparams.extensions = &ext2_;
    ext2_.type = &ext2_type_;
    ext2_.next = 0;
    ext2_type_.encode = writeFidelityParams;
    ext2_type_.arg = &fidelity_params;
    ClearcutSerializer::FillHistograms(pc,evt);
    VectorStream str {&evt_ser, 0};
    pb_ostream_t stream = {VectorStream::Write, &str, SIZE_MAX, 0};
    pb_encode(&stream, logs_proto_tuningfork_TuningForkLogEvent_fields, &evt);
}

} // namespace tuningfork {