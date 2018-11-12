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

#include "tuningfork.h"
#include "histogram.h"
#include "prong.h"
#include "uploadthread.h"
#include "Trace.h"

#include "tuningfork.pb.h"
#include "tuningfork_clearcut_log.pb.h"

#include <inttypes.h>
#include <android/log.h>
#include <vector>
#include <map>
#include <memory>
#include <chrono>
#include <sstream>


#define LOG_ERROR(MSG) __android_log_print(ANDROID_LOG_ERROR, "TuningFork", MSG )

/* Annotations come into tuning fork as a serialized protobuf. The protobuf can only have
 * enums in it. We form an integer annotation id from the annotation interpreted as a mixed-radix
 * number. E.g. say we have the following in the proto:
 * enum A { A_1 = 1, A_2 = 2, A_3 = 3};
 * enum B { B_1 = 1, B_2 = 2};
 * enum C { C_1 = 1};
 * extend Annotation { optional A a = 1; optional B b = 2; optional C c = 3};
 * Then a serialization might be:
 * 0x16 0x01
 * Here, 'a' and 'c' are missing and 'b' has the value B_1. Note the shift of 3 bits for the key.
 * https://developers.google.com/protocol-buffers/docs/encoding
 *
 * Assume we have 2 possible instrumentation keys: NUM_IKEY = 2
 *
 * The annotation id then is (0 + 1*4 + 0)*NUM_IKEY = 8, where the factor of 4 comes from the radix
 * associated with 'a', i.e. 3 values for enum A + option missing
 *
 * A compound id is formed from the annotation id and the instrument key:
 * compound_id = annotation_id + instrument_key;
 *
 * So for instrument key 1, the compound_id with the above annotation is 9
 *
 * This compound_id is used to look up a histogram in the ProngCache.
 *
 * annotation_radix_mult_ stores the multiplied radixes, so for the above, it is:
 * [4 4*3 4*3*2] = [4 12 24]
 * and the maximum number of annotations is 24
 *
 * */

namespace tuningfork {

using ::com::google::tuningfork::FidelityParams;
using ::com::google::tuningfork::Settings;
using ::com::google::tuningfork::Annotation;
using ::logs::proto::tuningfork::TuningForkLogEvent;
using ::logs::proto::tuningfork::TuningForkHistogram;

typedef uint64_t AnnotationId;

class MonoTimeProvider : public ITimeProvider {
public:
    virtual TimePoint NowNs() {
        return std::chrono::steady_clock::now();
    }
};

std::unique_ptr<MonoTimeProvider> s_mono_time_provider = std::make_unique<MonoTimeProvider>();

class TuningForkImpl {
private:
    Settings settings_;
    std::unique_ptr<ProngCache> prong_caches_[2];
    ProngCache *current_prong_cache_;
    TimePoint last_submit_time_ns_;
    std::unique_ptr<Trace> trace_;
    std::vector<TimePoint> live_traces_;
    Backend *backend_;
    UploadThread upload_thread_;
    SerializedAnnotation current_annotation_;
    std::vector<int> annotation_radix_mult_;
    AnnotationId current_annotation_id_;
    ITimeProvider *time_provider_;
    std::vector<Settings::Histogram> histogram_settings_;
public:
    TuningForkImpl(const Settings &settings,
                   Backend *backend,
                   ITimeProvider *time_provider_) : settings_(settings),
                                                    trace_(Trace::create()),
                                                    backend_(backend),
                                                    upload_thread_(backend),
                                                    current_annotation_id_(0),
                                                    time_provider_(time_provider_) {
        if (time_provider_ == nullptr)
            time_provider_ = s_mono_time_provider.get();
        last_submit_time_ns_ = time_provider_->NowNs();

        InitHistogramSettings();
        InitAnnotationRadixes();

        size_t max_num_prongs_ = 0;
        int max_ikeys = settings.aggregation_strategy().max_instrumentation_keys();
        if (annotation_radix_mult_.size() == 0 || max_ikeys == 0)
            LOG_ERROR("Neither max_annotations nor max_instrumentation_keys can be zero");
        else
            max_num_prongs_ = max_ikeys * annotation_radix_mult_.back();
        auto serializeId = [this](uint64_t id) { return SerializeAnnotationId(id); };
        prong_caches_[0] = std::make_unique<ProngCache>(max_num_prongs_, max_ikeys,
                                                        histogram_settings_, serializeId);
        prong_caches_[1] = std::make_unique<ProngCache>(max_num_prongs_, max_ikeys,
                                                        histogram_settings_, serializeId);
        current_prong_cache_ = prong_caches_[0].get();
        live_traces_.resize(max_num_prongs_);
        for (auto &t: live_traces_) t = TimePoint::min();
    }

    ~TuningForkImpl() {
    }

    void InitHistogramSettings();

    void InitAnnotationRadixes();

    bool GetFidelityParameters(ProtobufSerialization &fidelityParams, size_t timeout_ms);

    void SetCurrentAnnotation(const ProtobufSerialization &annotation);

    void FrameTick(InstrumentationKey id);

    void FrameDeltaTimeNanos(InstrumentationKey id, Duration dt);

    TraceHandle StartTrace(InstrumentationKey key);

    void EndTrace(TraceHandle);

private:
    Prong *TickNanos(uint64_t compound_id, TimePoint t);

    Prong *TraceNanos(uint64_t compound_id, Duration dt);

    void CheckForSubmit(TimePoint t_ns, Prong *prong);

    bool ShouldSubmit(TimePoint t_ns, Prong *prong);

    AnnotationId DecodeAnnotationSerialization(const SerializedAnnotation &ser);

    int GetInstrumentationKey(uint64_t compoundId) {
        return compoundId % settings_.aggregation_strategy().max_instrumentation_keys();
    }

    uint64_t MakeCompoundId(InstrumentationKey k, AnnotationId a) {
        return k + a;
    }

    SerializedAnnotation SerializeAnnotationId(uint64_t);
};

std::unique_ptr<TuningForkImpl> s_impl;

void Init(const ProtobufSerialization &settings_ser,
          Backend *backend,
          ITimeProvider *time_provider) {
    Settings settings;
    SerializationToProtobuf(settings_ser, settings);
    s_impl = std::make_unique<TuningForkImpl>(settings, backend,
                                              time_provider);
}

bool GetFidelityParameters(ProtobufSerialization &params, size_t timeout_ms) {
    if (!s_impl) {
        LOG_ERROR("Failed to get TuningFork instance");
        return false;
    } else
        return s_impl->GetFidelityParameters(params, timeout_ms);
}

void FrameTick(InstrumentationKey id) {
    if (!s_impl) {
        LOG_ERROR("Failed to get TuningFork instance");
    } else
        s_impl->FrameTick(id);
}

void FrameDeltaTimeNanos(InstrumentationKey id, Duration dt) {
    if (!s_impl) {
        LOG_ERROR("Failed to get TuningFork instance");
    } else s_impl->FrameDeltaTimeNanos(id, dt);
}

TraceHandle StartTrace(InstrumentationKey key) {
    if (!s_impl) {
        LOG_ERROR("Failed to get TuningFork instance");
        return 0;
    } else return s_impl->StartTrace(key);
}

void EndTrace(TraceHandle h) {
    if (!s_impl) {
        LOG_ERROR("Failed to get TuningFork instance");
    } else
        s_impl->EndTrace(h);
}

void SetCurrentAnnotation(const ProtobufSerialization &ann) {
    if (!s_impl) {
        LOG_ERROR("Failed to get TuningFork instance");
    } else
        s_impl->SetCurrentAnnotation(ann);
}

constexpr int kKeyError = -1;
constexpr AnnotationId kAnnotationError = -1;
constexpr uint64_t kStreamError = -1;

void TuningForkImpl::SetCurrentAnnotation(const ProtobufSerialization &annotation) {
    current_annotation_ = annotation;
    auto id = DecodeAnnotationSerialization(annotation);
    if (id == kAnnotationError)
        current_annotation_id_ = 0;
    else
        current_annotation_id_ = id;
}

// This is a protobuf 1-based index
int GetKeyIndex(uint8_t b) {
    int type = b & 0x7;
    if (type != 0) return kKeyError;
    return b >> 3;
}

uint64_t GetBase128IntegerFromByteStream(const std::vector<uint8_t> &bytes, int &index) {
    uint64_t m = 0;
    uint64_t r = 0;
    while (index < bytes.size() && m <= (64 - 7)) {
        auto b = bytes[index];
        r |= (((uint64_t) b) & 0x7f) << m;
        if ((b & 0x80) != 0) m += 7;
        else return r;
        ++index;
    }
    return kStreamError;
}

void WriteBase128IntToStream(uint64_t x, std::vector<uint8_t> &bytes) {
    while (x) {
        uint8_t a = x & 0x7f;
        int b = x & 0xffffffffffffff80;
        if (b) {
            bytes.push_back(a | 0x80);
            x >>= 7;
        } else {
            bytes.push_back(a);
            return;
        }
    }
}

AnnotationId TuningForkImpl::DecodeAnnotationSerialization(const SerializedAnnotation &ser) {
    AnnotationId result = 0;
    for (int i = 0; i < ser.size(); ++i) {
        int key = GetKeyIndex(ser[i]);
        if (key == kKeyError)
            return kAnnotationError;
        // Convert to 0-based index
        --key;
        if (key >= annotation_radix_mult_.size())
            return kAnnotationError;
        ++i;
        if (i >= ser.size())
            return kAnnotationError;
        uint64_t value = GetBase128IntegerFromByteStream(ser, i);
        if (value == kStreamError)
            return kAnnotationError;
        // Check the range of the value
        if (value == 0 || value >= annotation_radix_mult_[key])
            return kAnnotationError;
        // We don't allow enums with more that 255 values
        if (value > 0xff)
            return kAnnotationError;
        if (key > 0)
            result += annotation_radix_mult_[key - 1] * value;
        else
            result += value;
    }
    // Shift over to leave room for the instrument id
    return result * settings_.aggregation_strategy().max_instrumentation_keys();
}

SerializedAnnotation TuningForkImpl::SerializeAnnotationId(uint64_t id) {
    SerializedAnnotation ann;
    uint64_t x = id / settings_.aggregation_strategy().max_instrumentation_keys();
    for (int i = 0; i < annotation_radix_mult_.size(); ++i) {
        int value = x % annotation_radix_mult_[i];
        if (value > 0) {
            int key = (i + 1) << 3;
            ann.push_back(key);
            WriteBase128IntToStream(value, ann);
        }
        x /= annotation_radix_mult_[i];
    }
    return ann;
}

bool TuningForkImpl::GetFidelityParameters(ProtobufSerialization &params_ser, size_t timeout_ms) {
    auto result = backend_->GetFidelityParams(params_ser, timeout_ms);
    if (result) {
        upload_thread_.SetCurrentFidelityParams(params_ser);
    }
    return result;
}

TraceHandle TuningForkImpl::StartTrace(InstrumentationKey key) {
    trace_->beginSection("TFTrace");
    uint64_t h = MakeCompoundId(key, current_annotation_id_);
    live_traces_[h] = time_provider_->NowNs();
    return h;
}

void TuningForkImpl::EndTrace(TraceHandle h) {
    trace_->endSection();
    auto i = live_traces_[h];
    if (i != TimePoint::min())
        TraceNanos(h, time_provider_->NowNs() - i);
    live_traces_[h] = TimePoint::min();
}

void TuningForkImpl::FrameTick(InstrumentationKey key) {
    trace_->beginSection("TFTick");

    auto t = time_provider_->NowNs();
    auto compound_id = MakeCompoundId(key, current_annotation_id_);
    auto p = TickNanos(compound_id, t);
    if (p)
        CheckForSubmit(t, p);

    trace_->endSection();
}

void TuningForkImpl::FrameDeltaTimeNanos(InstrumentationKey key, Duration dt) {

    auto compound_d = MakeCompoundId(key, current_annotation_id_);
    auto p = TraceNanos(compound_d, dt);
    if (p)
        CheckForSubmit(time_provider_->NowNs(), p);
}

Prong *TuningForkImpl::TickNanos(uint64_t compound_id, TimePoint t) {
    // Find the appropriate histogram and add this time
    Prong *p = current_prong_cache_->Get(compound_id);
    if (p)
        p->Tick(t);
    else
        __android_log_print(ANDROID_LOG_WARN, "TuningFork",
                            "Bad id or limit of number of prongs reached");
    return p;
}

Prong *TuningForkImpl::TraceNanos(uint64_t compound_id, Duration dt) {
    // Find the appropriate histogram and add this time
    Prong *h = current_prong_cache_->Get(compound_id);
    if (h)
        h->Trace(dt);
    else
        __android_log_print(ANDROID_LOG_WARN, "TuningFork",
                            "Bad id or limit of number of prongs reached");
    return h;
}

bool TuningForkImpl::ShouldSubmit(TimePoint t_ns, Prong *prong) {
    switch (settings_.aggregation_strategy().method()) {
        case Settings::AggregationStrategy::TIME_BASED:
            return (t_ns - last_submit_time_ns_) >=
                   std::chrono::milliseconds(
                       settings_.aggregation_strategy().intervalms_or_count());
        case Settings::AggregationStrategy::TICK_BASED:
            if (prong)
                return prong->Count() >= settings_.aggregation_strategy().intervalms_or_count();
    }
    return false;
}

void TuningForkImpl::CheckForSubmit(TimePoint t_ns, Prong *prong) {
    if (ShouldSubmit(t_ns, prong)) {
        if (upload_thread_.Submit(current_prong_cache_)) {
            if (current_prong_cache_ == prong_caches_[0].get()) {
                prong_caches_[1]->Clear();
                current_prong_cache_ = prong_caches_[1].get();
            } else {
                prong_caches_[0]->Clear();
                current_prong_cache_ = prong_caches_[0].get();
            }
        }
        last_submit_time_ns_ = t_ns;
    }
}

void TuningForkImpl::InitHistogramSettings() {
    Settings::Histogram default_histogram;
    default_histogram.set_instrument_key(-1);
    default_histogram.set_bucket_min(0);
    default_histogram.set_bucket_max(0);
    default_histogram.set_n_buckets(Histogram::kDefaultNumBuckets);
    auto getHistogramSettings = [&](int ikey) {
        for (int i = 0; i < settings_.histograms_size(); ++i) {
            const Settings::Histogram &h = settings_.histograms(i);
            if (ikey == h.instrument_key())
                return h;
        }
        return default_histogram;
    };
    int nHist = settings_.histograms_size();
    int n = settings_.aggregation_strategy().max_instrumentation_keys();
    histogram_settings_.resize(n);
    for (int i = 0; i < n; ++i) {
        histogram_settings_[i].CopyFrom(getHistogramSettings(i));
    }
}

void TuningForkImpl::InitAnnotationRadixes() {
    int n = settings_.aggregation_strategy().annotation_enum_size_size();
    if (n == 0) {
        // With no annotations, we just have 1 possible prong per key
        annotation_radix_mult_.resize(1);
        annotation_radix_mult_[0] = 1;
    } else {
        annotation_radix_mult_.resize(n);
        int r = 1;
        for (int i = 0; i < n; ++i) {
            r *= settings_.aggregation_strategy().annotation_enum_size(i) + 1;
            annotation_radix_mult_[i] = r;
        }
    }
}

} // namespace tuningfork {
