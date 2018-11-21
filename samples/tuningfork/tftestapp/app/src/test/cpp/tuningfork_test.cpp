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

#include "tuningfork_test.h"

#include "tuningfork/protobuf_util.h"
#include "tuningfork/tuningfork.h"

#include "full/tuningfork.pb.h"
#include "full/tuningfork_clearcut_log.pb.h"
#include "full/tuningfork_extensions.pb.h"

#include <vector>
#include <mutex>

using namespace tuningfork;

namespace tuningfork_test {

using ::com::google::tuningfork::FidelityParams;
using ::com::google::tuningfork::Settings;
using ::com::google::tuningfork::Annotation;
using ::logs::proto::tuningfork::TuningForkLogEvent;
using ::logs::proto::tuningfork::TuningForkHistogram;

class TestBackend : public DebugBackend {
public:
    TestBackend(std::shared_ptr<std::condition_variable> cv_,
                      std::shared_ptr<std::mutex> mutex_) : cv(cv_), mutex(mutex_) {}

    bool Process(const ProtobufSerialization &evt_ser) override {
        {
            std::lock_guard<std::mutex> lock(*mutex);
            TuningForkLogEvent evt;
            Deserialize(evt_ser, evt);
#ifdef PROTOBUF_LITE
            result = evt.SerializeAsString();
#else
            result = evt.DebugString();
#endif
        }
        cv->notify_all();
        return true;
    }

    std::string result;
    std::shared_ptr<std::condition_variable> cv;
    std::shared_ptr<std::mutex> mutex;
};

// Increment time with a known tick size
class TestTimeProvider : public ITimeProvider {
public:
    TestTimeProvider(Duration tickSizeNs_ = std::chrono::nanoseconds(1)) : tickSizeNs(
        tickSizeNs_) {}

    TimePoint t;
    Duration tickSizeNs;

    TimePoint NowNs() override {
        t += std::chrono::nanoseconds(tickSizeNs);
        return t;
    }
};

std::shared_ptr<std::condition_variable> cv = std::make_shared<std::condition_variable>();
std::shared_ptr<std::mutex> rmutex = std::make_shared<std::mutex>();
TestBackend testBackend(cv, rmutex);
TestTimeProvider timeProvider;

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
std::string TestEndToEnd() {
    const int NTICKS = 101; // note the first tick doesn't add anything to the histogram
    auto settings = TestSettings(Settings::AggregationStrategy::TICK_BASED, NTICKS - 1, 1, {});
    tuningfork::Init(Serialize(settings), &testBackend, &timeProvider);
    std::unique_lock<std::mutex> lock(*rmutex);
    for (int i = 0; i < NTICKS; ++i)
        tuningfork::FrameTick(TFTICK_SYSCPU);
    // Wait for the upload thread to complete writing the string
    cv->wait(lock);
    return testBackend.result;
}

std::string TestEndToEndWithAnnotation() {
    const int NTICKS = 101; // note the first tick doesn't add anything to the histogram
    // {3} is the number of values in the Level enum in tuningfork_extensions.proto
    auto settings = TestSettings(Settings::AggregationStrategy::TICK_BASED, NTICKS - 1, 2, {3});
    tuningfork::Init(Serialize(settings), &testBackend, &timeProvider);
    Annotation ann;
    ann.SetExtension(level, LEVEL_1);
    tuningfork::SetCurrentAnnotation(Serialize(ann));
    std::unique_lock<std::mutex> lock(*rmutex);
    for (int i = 0; i < NTICKS; ++i)
        tuningfork::FrameTick(TFTICK_SYSGPU);
    // Wait for the upload thread to complete writing the string
    cv->wait(lock);
    return testBackend.result;
}

std::string TestEndToEndTimeBased() {
    const int NTICKS = 101; // note the first tick doesn't add anything to the histogram
    TestTimeProvider timeProvider(std::chrono::milliseconds(100)); // Tick in 100ms intervals
    auto settings = TestSettings(Settings::AggregationStrategy::TIME_BASED, 10100, 1, {});
    tuningfork::Init(Serialize(settings), &testBackend, &timeProvider);
    std::unique_lock<std::mutex> lock(*rmutex);
    for (int i = 0; i < NTICKS; ++i)
        tuningfork::FrameTick(TFTICK_SYSCPU);
    // Wait for the upload thread to complete writing the string
    cv->wait(lock);
    return testBackend.result;
}

std::string TestEndToEndWithStaticHistogram() {
    const int NTICKS = 101; // note the first tick doesn't add anything to the histogram
    TestTimeProvider timeProvider(std::chrono::milliseconds(100)); // Tick in 100ms intervals
    auto settings = TestSettings(Settings::AggregationStrategy::TIME_BASED,
                                 10100, 1, {}, {{98, 102, 10}});
    tuningfork::Init(Serialize(settings), &testBackend, &timeProvider);
    std::unique_lock<std::mutex> lock(*rmutex);
    for (int i = 0; i < NTICKS; ++i)
        tuningfork::FrameTick(TFTICK_SYSCPU);
    // Wait for the upload thread to complete writing the string
    cv->wait(lock);
    return testBackend.result;
}

}
