#include <jni.h>
#include "tuningfork/tuningfork.h"
#include "tuningfork.pb.h"
#include "tuningfork_clearcut_log.pb.h"
#include <android/log.h>

using ::com::google::tuningfork::FidelityParams;
using ::com::google::tuningfork::Settings;
using ::com::google::tuningfork::Annotation;
using ::logs::proto::tuningfork::TuningForkLogEvent;
using ::logs::proto::tuningfork::TuningForkHistogram;

namespace {
struct HistogramSettings {
    float start, end;
    int nBuckets;
};
Settings TestSettings(Settings::AggregationStrategy::Submission method, int n_ticks, int n_keys,
                      std::vector<int> annotation_size, const std::vector<HistogramSettings>& hists = {}) {
    // Make sure we set all required fields
    Settings s;
    s.mutable_aggregation_strategy()->set_method(method);
    s.mutable_aggregation_strategy()->set_intervalms_or_count(n_ticks);
    s.mutable_aggregation_strategy()->set_max_instrumentation_keys(n_keys);
    for(int i=0;i<annotation_size.size();++i)
        s.mutable_aggregation_strategy()->add_annotation_enum_size(annotation_size[i]);
    int i=0;
    for(auto& h: hists) {
        auto sh = s.add_histograms();
        sh->set_bucket_min(h.start);
        sh->set_bucket_max(h.end);
        sh->set_n_buckets(h.nBuckets);
        sh->set_instrument_key(i++);
    }
    return s;
}
} // namespace {

extern "C" {

JNIEXPORT void JNICALL
Java_com_google_tuningfork_TFTestActivity_nInit(JNIEnv */*env*/, jobject /*activity*/) {
    Settings s = TestSettings(Settings::AggregationStrategy::TIME_BASED, 10000, 1, {}, {{14, 19, 10}});
//    size_t n = s.ByteSize();
//    std::vector<uint8_t> bytes(n);
//    uint8_t* ptr = &bytes[0];
//    s.SerializeToArray(ptr, n);
//    CProtobufSerialization ser { ptr, n, nullptr };
    tuningfork::Init(tuningfork::Serialize(s));
}
JNIEXPORT void JNICALL
Java_com_google_tuningfork_TFTestActivity_nOnChoreographer(JNIEnv */*env*/, jobject /*activity*/, jlong /*frameTimeNanos*/) {
    tuningfork::FrameTick(tuningfork::TFTICK_SYSCPU);
}

}