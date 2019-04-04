/*
 * Copyright 2019 The Android Open Source Project
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

#include "tuningfork/protobuf_util.h"
#include "tuningfork/tuningfork_internal.h"

#include "full/tuningfork.pb.h"
#include "full/tuningfork_clearcut_log.pb.h"
#include "full/dev_tuningfork.pb.h"

#include "gtest/gtest.h"

#include <vector>
#include <mutex>

#define LOG_TAG "TFTest"
#include "Log.h"

using namespace tuningfork;

namespace tuningfork_test {

using ::com::google::tuningfork::FidelityParams;
using ::com::google::tuningfork::Annotation;
using ::logs::proto::tuningfork::TuningForkLogEvent;
using ::logs::proto::tuningfork::TuningForkHistogram;

class TestBackend : public DebugBackend {
public:
    TestBackend(std::shared_ptr<std::condition_variable> cv_,
                      std::shared_ptr<std::mutex> mutex_) : cv(cv_), mutex(mutex_) {}

    bool Process(const ProtobufSerialization &evt_ser) override {
        ALOGI("Process");
        {
            std::lock_guard<std::mutex> lock(*mutex);
            Deserialize(evt_ser, result);
        }
        cv->notify_all();
        return true;
    }

    void clear() { result = {}; }

    TuningForkLogEvent result;
    std::shared_ptr<std::condition_variable> cv;
    std::shared_ptr<std::mutex> mutex;
};

class TestParamsLoader : public ParamsLoader {
public:
    bool GetFidelityParams(ProtobufSerialization &fidelity_params, uint32_t timeout_ms) override {
        return false;
    }
};

// Increment time with a known tick size
class TestTimeProvider : public ITimeProvider {
public:
    TestTimeProvider(Duration tickSizeNs_ = std::chrono::milliseconds(20))
        : tickSizeNs(tickSizeNs_) {}

    TimePoint t;
    Duration tickSizeNs;

    TimePoint NowNs() override {
        t += tickSizeNs;
        return t;
    }
};

std::shared_ptr<std::condition_variable> cv = std::make_shared<std::condition_variable>();
std::shared_ptr<std::mutex> rmutex = std::make_shared<std::mutex>();
TestBackend testBackend(cv, rmutex);
TestParamsLoader paramsLoader;
TestTimeProvider timeProvider;
ExtraUploadInfo extra_upload_info = {};

TFSettings TestSettings(TFAggregationStrategy::TFSubmissionPolicy method, int n_ticks, int n_keys,
                      std::vector<int> annotation_size,
                      const std::vector<TFHistogram>& hists = {}) {
    // Make sure we set all required fields
    TFSettings s;
    s.aggregation_strategy.method = method;
    s.aggregation_strategy.intervalms_or_count = n_ticks;
    s.aggregation_strategy.max_instrumentation_keys = n_keys;
    s.aggregation_strategy.n_annotation_enum_size = annotation_size.size();
    auto n_ann_bytes = sizeof(uint32_t)*annotation_size.size();
    s.aggregation_strategy.annotation_enum_size = (uint32_t*)malloc(n_ann_bytes);
    memcpy(s.aggregation_strategy.annotation_enum_size, annotation_size.data(), n_ann_bytes);
    s.n_histograms = hists.size();
    auto n_hist_bytes = sizeof(TFHistogram)*hists.size();
    s.histograms = (TFHistogram*)malloc(n_hist_bytes);
    memcpy(s.histograms, hists.data(), n_hist_bytes);
    return s;
}
const Duration test_wait_time = std::chrono::seconds(1);
const TuningForkLogEvent& TestEndToEnd() {
    const int NTICKS = 101; // note the first tick doesn't add anything to the histogram
    auto settings = TestSettings(TFAggregationStrategy::TICK_BASED, NTICKS - 1, 1, {});
    tuningfork::Init(settings, extra_upload_info, &testBackend, &paramsLoader, &timeProvider);
    std::unique_lock<std::mutex> lock(*rmutex);
    for (int i = 0; i < NTICKS; ++i)
        tuningfork::FrameTick(TFTICK_SYSCPU);
    // Wait for the upload thread to complete writing the string
    EXPECT_TRUE(cv->wait_for(lock, test_wait_time)==std::cv_status::no_timeout) << "Timeout";

    return testBackend.result;
}

const TuningForkLogEvent& TestEndToEndWithAnnotation() {
    testBackend.clear();
    const int NTICKS = 101; // note the first tick doesn't add anything to the histogram
    // {3} is the number of values in the Level enum in tuningfork_extensions.proto
    auto settings = TestSettings(TFAggregationStrategy::TICK_BASED, NTICKS - 1, 2, {3});
    tuningfork::Init(settings, extra_upload_info, &testBackend, &paramsLoader, &timeProvider);
    Annotation ann;
    ann.set_level(com::google::tuningfork::LEVEL_1);
    tuningfork::SetCurrentAnnotation(Serialize(ann));
    std::unique_lock<std::mutex> lock(*rmutex);
    for (int i = 0; i < NTICKS; ++i)
        tuningfork::FrameTick(TFTICK_SYSGPU);
    // Wait for the upload thread to complete writing the string
    EXPECT_TRUE(cv->wait_for(lock, test_wait_time)==std::cv_status::no_timeout) << "Timeout";
    return testBackend.result;
}

const TuningForkLogEvent& TestEndToEndTimeBased() {
    testBackend.clear();
    const int NTICKS = 101; // note the first tick doesn't add anything to the histogram
    TestTimeProvider timeProvider(std::chrono::milliseconds(100)); // Tick in 100ms intervals
    auto settings = TestSettings(TFAggregationStrategy::TIME_BASED, 10100, 1, {}, {{0, 50,150,10}});
    tuningfork::Init(settings, extra_upload_info, &testBackend, &paramsLoader, &timeProvider);
    std::unique_lock<std::mutex> lock(*rmutex);
    for (int i = 0; i < NTICKS; ++i)
        tuningfork::FrameTick(TFTICK_SYSCPU);
    // Wait for the upload thread to complete writing the string
    EXPECT_TRUE(cv->wait_for(lock, test_wait_time)==std::cv_status::no_timeout) << "Timeout";
    return testBackend.result;
}

void CheckEvent(const std::string& name, const TuningForkLogEvent& result,const TuningForkLogEvent& expected) {
    EXPECT_EQ(result.histograms_size(), expected.histograms_size()) << name << ": N histograms";
    auto n_hist = result.histograms_size();
    for(int i=0;i<n_hist;++i) {
        auto& a = result.histograms(i);
        auto& b = expected.histograms(i);
        EXPECT_EQ(a.instrument_id(), b.instrument_id()) << name << ": histogram " << i << " id";
        ASSERT_EQ(a.counts_size(), b.counts_size()) << name << ": histogram " << i << " counts";
        for(int c=0;c<a.counts_size(); ++c) {
            EXPECT_EQ(a.counts(c), b.counts(c)) << name << ": histogram " << i << " count " << c;
        }
        ASSERT_EQ(a.has_annotation(), b.has_annotation()) << name << ": annotation";
        if(a.has_annotation()) {
            EXPECT_EQ(a.annotation(), b.annotation()) << name << ": annotation value";
        }
    }
}

TEST(TuningForkTest, EndToEnd) {
    auto& result = TestEndToEnd();
    TuningForkLogEvent expected = {};
    auto h = expected.add_histograms();
    h->set_instrument_id(0);
    for(int i=0;i<32;++i)
        h->add_counts(i==11?100:0);
    CheckEvent("Base", result, expected);
}

TEST(TuningForkTest, TestEndToEndWithAnnotation) {
    auto& result = TestEndToEndWithAnnotation();
    TuningForkLogEvent expected = {};
    auto h = expected.add_histograms();
    h->set_instrument_id(1);
    for(int i=0;i<32;++i)
        h->add_counts(i==11?100:0);
    char ann[] = "\010\001";
    h->set_annotation(ann);
    CheckEvent("Annotation", result, expected);
}

TEST(TuningForkTest, TestEndToEndTimeBased) {
    auto& result = TestEndToEndTimeBased();
    TuningForkLogEvent expected = {};
    auto h = expected.add_histograms();
    h->set_instrument_id(0);
    for(int i=0;i<12;++i)
        h->add_counts(i==6?100:0);
    CheckEvent("TimeBased", result, expected);
}

} // namespace tuningfork_test
