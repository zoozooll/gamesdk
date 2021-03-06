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

#include "tuningfork_internal.h"

#include <cinttypes>
#include <vector>
#include <map>
#include <memory>
#include <chrono>
#include <sstream>
#include <atomic>

#define LOG_TAG "TuningFork"
#include "Log.h"
#include "Trace.h"

#include "histogram.h"
#include "prong.h"
#include "uploadthread.h"
#include "clearcutserializer.h"
#include "clearcut_backend.h"
#include "annotation_util.h"
#include "crash_handler.h"

/* Annotations come into tuning fork as a serialized protobuf. The protobuf can only have
 * enums in it. We form an integer annotation id from the annotation interpreted as a mixed-radix
 * number. E.g. say we have the following in the proto:
 * enum A { A_1 = 1, A_2 = 2, A_3 = 3};
 * enum B { B_1 = 1, B_2 = 2};
 * enum C { C_1 = 1};
 * message Annotation { optional A a = 1; optional B b = 2; optional C c = 3};
 * Then a serialization of 'b : B_1' might be:
 * 0x16 0x01
 * https://developers.google.com/protocol-buffers/docs/encoding
 * Note the shift of 3 bits for the key.
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
    CrashHandler crash_handler_;
    Settings settings_;
    std::unique_ptr<ProngCache> prong_caches_[2];
    ProngCache *current_prong_cache_;
    TimePoint last_submit_time_ns_;
    std::unique_ptr<gamesdk::Trace> trace_;
    std::vector<TimePoint> live_traces_;
    Backend *backend_;
    ParamsLoader *loader_;
    UploadThread upload_thread_;
    SerializedAnnotation current_annotation_;
    std::vector<uint32_t> annotation_radix_mult_;
    AnnotationId current_annotation_id_;
    ITimeProvider *time_provider_;
    std::vector<InstrumentationKey> ikeys_;
    std::atomic<int> next_ikey_;
public:
    TuningForkImpl(const Settings& settings,
                   const ExtraUploadInfo& extra_upload_info,
                   Backend *backend,
                   ParamsLoader *loader,
                   ITimeProvider *time_provider) : settings_(settings),
                                trace_(gamesdk::Trace::create()),
                                backend_(backend),
                                loader_(loader),
                                upload_thread_(backend, extra_upload_info),
                                current_annotation_id_(0),
                                time_provider_(time_provider),
                                ikeys_(settings.aggregation_strategy.max_instrumentation_keys),
                                next_ikey_(0) {
        if (time_provider_ == nullptr) {
            time_provider_ = s_mono_time_provider.get();
        }
        last_submit_time_ns_ = time_provider_->NowNs();

        InitHistogramSettings();
        InitAnnotationRadixes();

        size_t max_num_prongs_ = 0;
        int max_ikeys = settings.aggregation_strategy.max_instrumentation_keys;

        if (annotation_radix_mult_.size() == 0 || max_ikeys == 0)
            ALOGE("Neither max_annotations nor max_instrumentation_keys can be zero");
        else
            max_num_prongs_ = max_ikeys * annotation_radix_mult_.back();
        auto serializeId = [this](uint64_t id) { return SerializeAnnotationId(id); };
        prong_caches_[0] = std::make_unique<ProngCache>(max_num_prongs_, max_ikeys,
                                                        settings_.histograms, serializeId);
        prong_caches_[1] = std::make_unique<ProngCache>(max_num_prongs_, max_ikeys,
                                                        settings_.histograms, serializeId);
        current_prong_cache_ = prong_caches_[0].get();
        live_traces_.resize(max_num_prongs_);
        for (auto &t: live_traces_) t = TimePoint::min();
        auto crash_callback = [this]()->bool {
            std::stringstream ss;
            ss << std::this_thread::get_id();
            TFErrorCode ret = this->Flush();
            ALOGI("Flush result : %d", ret);
            return true;
        };
        crash_handler_.Init(crash_callback);
        ALOGI("TuningFork initialized");
    }

    ~TuningForkImpl() {
    }

    void InitHistogramSettings();

    void InitAnnotationRadixes();

    // Returns true if the fidelity params were retrieved
    TFErrorCode GetFidelityParameters(JNIEnv* env, jobject context,
                               const std::string& url_base,
                               const std::string& api_key,
                               const ProtobufSerialization& defaultParams,
                               ProtobufSerialization &fidelityParams, uint32_t timeout_ms);

    // Returns the set annotation id or -1 if it could not be set
    uint64_t SetCurrentAnnotation(const ProtobufSerialization &annotation);

    TFErrorCode FrameTick(InstrumentationKey id);

    TFErrorCode FrameDeltaTimeNanos(InstrumentationKey id, Duration dt);

    // Fills handle with that to be used by EndTrace
    TFErrorCode StartTrace(InstrumentationKey key, TraceHandle& handle);

    TFErrorCode EndTrace(TraceHandle);

    void SetUploadCallback(void(*cbk)(const CProtobufSerialization*));

    TFErrorCode Flush();

    TFErrorCode Flush(TimePoint t_ns);

private:
    Prong *TickNanos(uint64_t compound_id, TimePoint t);

    Prong *TraceNanos(uint64_t compound_id, Duration dt);

    TFErrorCode CheckForSubmit(TimePoint t_ns, Prong *prong);

    bool ShouldSubmit(TimePoint t_ns, Prong *prong);

    AnnotationId DecodeAnnotationSerialization(const SerializedAnnotation &ser);

    uint32_t GetInstrumentationKey(uint64_t compoundId) {
        return compoundId % settings_.aggregation_strategy.max_instrumentation_keys;
    }

    TFErrorCode MakeCompoundId(InstrumentationKey k, AnnotationId a, uint64_t& id) {
        int key_index;
        auto err = GetOrCreateInstrumentKeyIndex(k, key_index);
        if (err!=TFERROR_OK) return err;
        id = key_index + a;
        return TFERROR_OK;
    }

    SerializedAnnotation SerializeAnnotationId(uint64_t);

    bool keyIsValid(InstrumentationKey key) const;

    TFErrorCode GetOrCreateInstrumentKeyIndex(InstrumentationKey key, int& index);

};

std::unique_ptr<TuningForkImpl> s_impl;

void CopySettings(const TFSettings &c_settings, Settings &settings) {
    auto& a = settings.aggregation_strategy;
    auto& ca = c_settings.aggregation_strategy;
    a.intervalms_or_count = ca.intervalms_or_count;
    a.max_instrumentation_keys = ca.max_instrumentation_keys;
    a.method = ca.method==TFAggregationStrategy::TICK_BASED?
                 Settings::AggregationStrategy::TICK_BASED:
                 Settings::AggregationStrategy::TIME_BASED;
    a.annotation_enum_size = std::vector<uint32_t>(ca.annotation_enum_size,
                                        ca.annotation_enum_size + ca.n_annotation_enum_size);
    settings.histograms = std::vector<TFHistogram>(c_settings.histograms,
                                        c_settings.histograms + c_settings.n_histograms);
}

TFErrorCode Init(const TFSettings &c_settings,
          const ExtraUploadInfo& extra_upload_info,
          Backend *backend,
          ParamsLoader *loader,
          ITimeProvider *time_provider) {
    Settings settings;
    CopySettings(c_settings, settings);
    s_impl = std::make_unique<TuningForkImpl>(settings, extra_upload_info, backend, loader,
                                              time_provider);
    return TFERROR_OK;
}

ClearcutBackend sBackend;
ProtoPrint sProtoPrint;
ParamsLoader sLoader;

TFErrorCode Init(const TFSettings &c_settings, JNIEnv* env, jobject context) {
    bool backendInited = sBackend.Init(env, context, &sProtoPrint)==TFERROR_OK;

    ExtraUploadInfo extra_upload_info = UploadThread::GetExtraUploadInfo(env, context);
    Backend* backend = nullptr;
    ParamsLoader* loader = nullptr;
    if(backendInited) {
        ALOGV("TuningFork.Clearcut: OK");
        backend = &sBackend;
        loader = &sLoader;
    }
    else {
        ALOGV("TuningFork.Clearcut: FAILED");
    }
    return Init(c_settings, extra_upload_info, backend, loader);
}

TFErrorCode GetFidelityParameters(JNIEnv* env, jobject context,
                           const std::string& url_base,
                           const std::string& api_key,
                           const ProtobufSerialization &defaultParams,
                           ProtobufSerialization &params, uint32_t timeout_ms) {
    if (!s_impl) {
        return TFERROR_TUNINGFORK_NOT_INITIALIZED;
    } else {
        return s_impl->GetFidelityParameters(env, context, url_base, api_key, defaultParams,
                                             params, timeout_ms);
    }
}

TFErrorCode FrameTick(InstrumentationKey id) {
    if (!s_impl) {
        return TFERROR_TUNINGFORK_NOT_INITIALIZED;
    } else {
        return s_impl->FrameTick(id);
    }
}

TFErrorCode FrameDeltaTimeNanos(InstrumentationKey id, Duration dt) {
    if (!s_impl) {
        return TFERROR_TUNINGFORK_NOT_INITIALIZED;
    } else {
        return s_impl->FrameDeltaTimeNanos(id, dt);
    }
}

TFErrorCode StartTrace(InstrumentationKey key, TraceHandle& handle) {
    if (!s_impl) {
        return TFERROR_TUNINGFORK_NOT_INITIALIZED;
    } else {
        return s_impl->StartTrace(key, handle);
    }
}

TFErrorCode EndTrace(TraceHandle h) {
    if (!s_impl) {
        return TFERROR_TUNINGFORK_NOT_INITIALIZED;
    } else {
        return s_impl->EndTrace(h);
    }
}

TFErrorCode SetCurrentAnnotation(const ProtobufSerialization &ann) {
    if (!s_impl) {
        return TFERROR_TUNINGFORK_NOT_INITIALIZED;
    } else {
        if (s_impl->SetCurrentAnnotation(ann)==-1) {
            return TFERROR_INVALID_ANNOTATION;
        } else {
            return TFERROR_OK;
        }
    }
}

TFErrorCode SetUploadCallback(void(*cbk)(const CProtobufSerialization*)) {
    if (!s_impl) {
        return TFERROR_TUNINGFORK_NOT_INITIALIZED;
    } else {
        s_impl->SetUploadCallback(cbk);
        return TFERROR_OK;
    }
}

TFErrorCode Flush() {
    if (!s_impl) {
        return TFERROR_TUNINGFORK_NOT_INITIALIZED;
    } else {
        return s_impl->Flush();
    }
}

// Return the set annotation id or -1 if it could not be set
uint64_t TuningForkImpl::SetCurrentAnnotation(const ProtobufSerialization &annotation) {
    current_annotation_ = annotation;
    auto id = DecodeAnnotationSerialization(annotation);
    if (id == annotation_util::kAnnotationError) {
        ALOGW("Error setting annotation of size %zu", annotation.size());
        current_annotation_id_ = 0;
        return annotation_util::kAnnotationError;
    }
    else {
        ALOGV("Set annotation id to %" PRIu64, id);
        current_annotation_id_ = id;
        return current_annotation_id_;
    }
}

AnnotationId TuningForkImpl::DecodeAnnotationSerialization(const SerializedAnnotation &ser) {
    auto id = annotation_util::DecodeAnnotationSerialization(ser, annotation_radix_mult_);
     // Shift over to leave room for the instrument id
    return id * settings_.aggregation_strategy.max_instrumentation_keys;
}

SerializedAnnotation TuningForkImpl::SerializeAnnotationId(AnnotationId id) {
    SerializedAnnotation ann;
    AnnotationId a = id / settings_.aggregation_strategy.max_instrumentation_keys;
    annotation_util::SerializeAnnotationId(a, ann, annotation_radix_mult_);
    return ann;
}

TFErrorCode TuningForkImpl::GetFidelityParameters(JNIEnv* env, jobject context,
                                           const std::string& url_base,
                                           const std::string& api_key,
                                           const ProtobufSerialization& defaultParams,
                                           ProtobufSerialization &params_ser, uint32_t timeout_ms) {
    if(loader_) {
        std::string experiment_id;
        auto result = loader_->GetFidelityParams(env, context,
                                 upload_thread_.GetExtraUploadInfo(env, context), url_base,
                                 api_key, params_ser, experiment_id, timeout_ms);
        upload_thread_.SetCurrentFidelityParams(params_ser, experiment_id);
        return result;
    }
    else
        return TFERROR_TUNINGFORK_NOT_INITIALIZED;
}
TFErrorCode TuningForkImpl::GetOrCreateInstrumentKeyIndex(InstrumentationKey key, int& index) {
    int nkeys = next_ikey_;
    for (int i=0; i<nkeys; ++i) {
        if (ikeys_[i] == key) {
            index = i;
            return TFERROR_OK;
        }
    }
    // Another thread could have incremented next_ikey while we were checking, but we
    //  mandate that different threads not use the same key, so we are OK adding
    //  our key, if we can.
    int next = next_ikey_++;
    if (next<ikeys_.size()) {
        ikeys_[next] = key;
        index = next;
        return TFERROR_OK;
    }
    else {
        next_ikey_--;
    }
    return TFERROR_INVALID_INSTRUMENT_KEY;
}
TFErrorCode TuningForkImpl::StartTrace(InstrumentationKey key, TraceHandle& handle) {
    auto err = MakeCompoundId(key, current_annotation_id_, handle);
    if (err!=TFERROR_OK) return err;
    trace_->beginSection("TFTrace");
    live_traces_[handle] = time_provider_->NowNs();
    return TFERROR_OK;
}

TFErrorCode TuningForkImpl::EndTrace(TraceHandle h) {
    if (h>=live_traces_.size()) return TFERROR_INVALID_TRACE_HANDLE;
    auto i = live_traces_[h];
    if (i != TimePoint::min()) {
        trace_->endSection();
        TraceNanos(h, time_provider_->NowNs() - i);
        live_traces_[h] = TimePoint::min();
        return TFERROR_OK;
    } else {
        return TFERROR_INVALID_TRACE_HANDLE;
    }
}

TFErrorCode TuningForkImpl::FrameTick(InstrumentationKey key) {
    uint64_t compound_id;
    auto err = MakeCompoundId(key, current_annotation_id_, compound_id);
    if (err!=TFERROR_OK) return err;
    trace_->beginSection("TFTick");
    auto t = time_provider_->NowNs();
    auto p = TickNanos(compound_id, t);
    if (p)
        CheckForSubmit(t, p);
    trace_->endSection();
    return TFERROR_OK;
}

TFErrorCode TuningForkImpl::FrameDeltaTimeNanos(InstrumentationKey key, Duration dt) {
    uint64_t compound_id;
    auto err = MakeCompoundId(key, current_annotation_id_, compound_id);
    if (err!=TFERROR_OK) return err;
    auto p = TraceNanos(compound_id, dt);
    if (p)
        CheckForSubmit(time_provider_->NowNs(), p);
    return TFERROR_OK;
}

Prong *TuningForkImpl::TickNanos(uint64_t compound_id, TimePoint t) {
    // Find the appropriate histogram and add this time
    Prong *p = current_prong_cache_->Get(compound_id);
    if (p)
        p->Tick(t);
    else
       ALOGW("Bad id or limit of number of prongs reached");
    return p;
}

Prong *TuningForkImpl::TraceNanos(uint64_t compound_id, Duration dt) {
    // Find the appropriate histogram and add this time
    Prong *h = current_prong_cache_->Get(compound_id);
    if (h)
        h->Trace(dt);
    else
        ALOGW("Bad id or limit of number of prongs reached");
    return h;
}

void TuningForkImpl::SetUploadCallback(void(*cbk)(const CProtobufSerialization*)) {
    upload_thread_.SetUploadCallback(cbk);
}


bool TuningForkImpl::ShouldSubmit(TimePoint t_ns, Prong *prong) {
    auto method = settings_.aggregation_strategy.method;
    auto count = settings_.aggregation_strategy.intervalms_or_count;
    switch (settings_.aggregation_strategy.method) {
        case Settings::AggregationStrategy::TIME_BASED:
            return (t_ns - last_submit_time_ns_) >=
                   std::chrono::milliseconds(count);
        case Settings::AggregationStrategy::TICK_BASED:
            if (prong)
                return prong->Count() >= count;
    }
    return false;
}

TFErrorCode TuningForkImpl::CheckForSubmit(TimePoint t_ns, Prong *prong) {
    TFErrorCode ret_code = TFERROR_OK;
    if (ShouldSubmit(t_ns, prong)) {
        ret_code = Flush(t_ns);
    }
    return ret_code;
}

void TuningForkImpl::InitHistogramSettings() {
    TFHistogram default_histogram;
    default_histogram.instrument_key = -1;
    default_histogram.bucket_min = 10;
    default_histogram.bucket_max = 40;
    default_histogram.n_buckets = Histogram::kDefaultNumBuckets;
    for(uint32_t i=0; i<settings_.aggregation_strategy.max_instrumentation_keys; ++i) {
        if(settings_.histograms.size()<=i) {
            ALOGW("Couldn't get histogram for key index %d. Using default histogram", i);
            settings_.histograms.push_back(default_histogram);
        }
        else {
            int index;
            GetOrCreateInstrumentKeyIndex(settings_.histograms[i].instrument_key, index);
        }
    }
    ALOGV("TFHistograms");
    for(uint32_t i=0; i< settings_.histograms.size(); ++i) {
        auto& h = settings_.histograms[i];
        ALOGV("ikey: %d min: %f max: %f nbkts: %d", h.instrument_key, h.bucket_min, h.bucket_max,
              h.n_buckets);
    }
}

void TuningForkImpl::InitAnnotationRadixes() {
    annotation_util::SetUpAnnotationRadixes(annotation_radix_mult_,
                                            settings_.aggregation_strategy.annotation_enum_size);
}

TFErrorCode TuningForkImpl::Flush() {
    auto t = time_provider_->NowNs();
    // Only allow manual submission a maximum of once per minute
    auto dt = t - last_submit_time_ns_;
    if (dt > std::chrono::seconds(60))
        return Flush(t);
    else
        return TFERROR_UPLOAD_TOO_FREQUENT;
}

TFErrorCode TuningForkImpl::Flush(TimePoint t_ns) {
    TFErrorCode ret_code;
    current_prong_cache_->SetInstrumentKeys(ikeys_);
    if (upload_thread_.Submit(current_prong_cache_)) {
        if (current_prong_cache_ == prong_caches_[0].get()) {
            prong_caches_[1]->Clear();
            current_prong_cache_ = prong_caches_[1].get();
        } else {
            prong_caches_[0]->Clear();
            current_prong_cache_ = prong_caches_[0].get();
        }
        ret_code = TFERROR_OK;
    } else {
        ret_code = TFERROR_PREVIOUS_UPLOAD_PENDING;
    }
    last_submit_time_ns_ = t_ns;
    return ret_code;
}

} // namespace tuningfork {
