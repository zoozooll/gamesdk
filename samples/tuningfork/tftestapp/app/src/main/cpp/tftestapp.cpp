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

#include "tuningfork/protobuf_util.h"
#include "tuningfork/tuningfork_internal.h"
#include "full/tuningfork.pb.h"
#include "full/tuningfork_clearcut_log.pb.h"
#include "full/tuningfork_extensions.pb.h"
#include <android/log.h>
#include <jni.h>
#include <tuningfork/clearcut_backend.h>

using ::com::google::tuningfork::FidelityParams;
using ::com::google::tuningfork::Settings;
using ::com::google::tuningfork::Annotation;
using ::logs::proto::tuningfork::TuningForkLogEvent;
using ::logs::proto::tuningfork::TuningForkHistogram;

namespace proto_tf = com::google::tuningfork;
namespace tf = tuningfork;

namespace {

struct HistogramSettings {
    float start, end;
    int nBuckets;
};
Settings TestSettings(Settings::AggregationStrategy::Submission method, int n_ticks, int n_keys,
                      std::vector<int> annotation_size,
                      const std::vector<HistogramSettings>& hists = {}) {
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
std::string ReplaceReturns(const std::string& s) {
    std::string r = s;
    for(int i=0;i<r.length();++i) {
        if(r[i]=='\n') r[i] = ',';
        if(r[i]=='\r') r[i] = ' ';
    }
    return r;
}
std::string PrettyPrintTuningForkLogEvent(const TuningForkLogEvent& evt) {
    std::stringstream eventStr;
    eventStr << "TuningForkLogEvent {\n";
    if(evt.has_fidelityparams()) {
        FidelityParams p;
        p.ParseFromArray(evt.fidelityparams().c_str(), evt.fidelityparams().length());
        eventStr << "  fidelityparams : " << ReplaceReturns(p.DebugString()) << "\n";
    }
    for(int i=0; i<evt.histograms_size(); ++i) {
        auto& h = evt.histograms(i);
        Annotation ann;
        ann.ParseFromArray(h.annotation().c_str(), h.annotation().length());
        bool first = true;
        eventStr << "  histogram {\n";
        eventStr << "    instrument_id : " << h.instrument_id() << "\n";
        eventStr << "    annotation : " << ReplaceReturns(ann.DebugString()) << "\n    counts : ";
        eventStr << "[";
        for(int j=0; j<h.counts_size(); ++j) {
            if (first)
                first = false;
            else
                eventStr << ",";
            eventStr << h.counts(j);
        }
        eventStr << "]\n  }\n";
    }
    if(evt.has_experiment_id()) {
        eventStr << "  experiment_id : " << evt.experiment_id() << "\n";
    }
    eventStr << "}";
    return eventStr.str();
}
tf::DebugBackend dbgBackend;
class LogcatBackend : public tf::Backend {
public:
    ~LogcatBackend() override {}
    bool Process(const tf::ProtobufSerialization &tuningfork_log_event) override {
        TuningForkLogEvent evt;
        tf::Deserialize(tuningfork_log_event, evt);
        __android_log_print(ANDROID_LOG_INFO, "TuningFork", "%s",
                            PrettyPrintTuningForkLogEvent(evt).c_str());
        return dbgBackend.Process(tuningfork_log_event);
    }
};

class PrettyProtoPrint : public tf::ProtoPrint {
public:
    ~PrettyProtoPrint() override  {}
    void Print(const tf::ProtobufSerialization &tuningfork_log_event) override  {
        TuningForkLogEvent evt;
        tf::Deserialize(tuningfork_log_event, evt);
        __android_log_print(ANDROID_LOG_INFO, "TuningFork.Clearcut logcat: ", "%s",
                            PrettyPrintTuningForkLogEvent(evt).c_str());
    }
};

class FidelityParamsLoader : public tf::ParamsLoader {
public :
    ~FidelityParamsLoader() override {}

    bool GetFidelityParams(tf::ProtobufSerialization &fidelity_params, size_t timeout_ms) override {
        FidelityParams p;
        p.set_lod(com::google::tuningfork::LOD_1);
        fidelity_params = tf::Serialize(p);
        return true;
    }
};



tf::ProtoPrint protoPrint;
tf::ClearcutBackend ccBackend;
tf::ParamsLoader paramsLoader;

PrettyProtoPrint prettyPrint;
LogcatBackend debugBackend;
FidelityParamsLoader debugLoader;


static int sLevel = proto_tf::Level_MIN;
void SetAnnotations() {
    __android_log_print(ANDROID_LOG_DEBUG, "TuningFork", "Setting level to %d", sLevel);
    if(proto_tf::Level_IsValid(sLevel)) {
        Annotation a;
        a.set_level((proto_tf::Level)sLevel);
        a.set_level2((proto_tf::Level)sLevel);
        tf::SetCurrentAnnotation(tf::Serialize(a));
    }
}

} // anonymous namespace

extern "C" {

JNIEXPORT void JNICALL
Java_com_google_tuningfork_TFTestActivity_nInit(JNIEnv *env, jobject activity) {
    Settings s = TestSettings(Settings::AggregationStrategy::TICK_BASED,
                              100, // Time in ms between events
                              1, // Number of instrumentation keys (we only use SYSCPU)
                              {4, 4}, // annotation enum sizes (4 levels)
                              {{14, // histogram minimum delta time in ms
                                19, // histogram maximum delta time in ms
                                70} // number of buckets between the max and min (there will be
                                  //   2 more for out-of-bounds ticks, too)
                              });

    bool isClearcutInited = ccBackend.Init(env, activity, &prettyPrint);
   // tf::Init(tf::Serialize(s), env, activity);

    // Clearcut will not be inited if gms is not available
    if(isClearcutInited)
        tf::Init(tf::Serialize(s),&ccBackend, &debugLoader);
    else
        tf::Init(tf::Serialize(s), &debugBackend, &debugLoader);

    tf::ProtobufSerialization defaultParams;
    tf::ProtobufSerialization params;
    if(!tf::GetFidelityParameters(defaultParams, params, 1000)) {
        __android_log_print(ANDROID_LOG_WARN, "TuningFork", "Could not get FidelityParams");
    }
    else {
        FidelityParams p;
        tf::Deserialize(params, p);
        std::string s = p.DebugString();
        __android_log_print(ANDROID_LOG_INFO, "TuningFork", "Got FidelityParams: %s", s.c_str());

    }
    SetAnnotations();
}
JNIEXPORT void JNICALL
Java_com_google_tuningfork_TFTestActivity_nOnChoreographer(JNIEnv */*env*/, jobject /*activity*/,
                                                           jlong /*frameTimeNanos*/) {
    tf::FrameTick(tf::TFTICK_SYSCPU);
    // After 600 ticks, switch to the next level
    static int tick_count = 0;
    ++tick_count;
    if(tick_count>=600) {
        ++sLevel;
        if(sLevel>proto_tf::Level_MAX) sLevel = proto_tf::Level_MIN;
        SetAnnotations();
        tick_count = 0;
    }
}

}
