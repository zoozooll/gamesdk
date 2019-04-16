#include <gtest/gtest.h>
#include "tuningfork/clearcutserializer.h"
#include "tuningfork/protobuf_nano_util.h"
#include "tuningfork/protobuf_util.h"
#include "nano/dev_tuningfork.pb.h"
#include "nano/tuningfork_clearcut_log.pb.h"
#include "full/dev_tuningfork.pb.h"
#include "full/tuningfork_clearcut_log.pb.h"

namespace serialization_test {

using ::com::google::tuningfork::StringTest;
using ::com::google::tuningfork::I64ArrTest;
using ::com::google::tuningfork::BytesTest;
using ::logs::proto::tuningfork::TuningForkHistogram;
using ::logs::proto::tuningfork::TuningForkLogEvent;
using namespace tuningfork;

void CheckString(const std::string& s) {
    std::vector<uint8_t> ser;
    VectorStream str {&ser, 0};
    pb_ostream_t stream = {VectorStream::Write, &str, SIZE_MAX, 0};
    const std::string* value_ptr = &s;
    const int field_num = com_google_tuningfork_StringTest_value_tag;
    ClearcutSerializer::writeString(&stream, &com_google_tuningfork_StringTest_fields[field_num-1],
                                    (void *const*)&value_ptr);
    StringTest full_stest;
    Deserialize(ser, full_stest);
    EXPECT_EQ( s, full_stest.value()) << "Bad string write";
}

template<typename T, typename U, typename S> void CheckArray(const std::vector<uint8_t>& ser,
                    const std::vector<S>& arr, U* (T::*arr_getter)()) {
    T full;
    Deserialize(ser, full);
    auto& v = *((full.*arr_getter)());
    EXPECT_EQ(arr.size(), v.size()) << "Bad array size";
    const S* pv = arr.data();
    for (auto& x: v) {
        EXPECT_EQ(x, *pv++) << "Bad array value";
    }
}

void CheckCpuFreqs(const std::vector<uint64_t>& arr) {
    std::vector<uint8_t> ser;
    VectorStream str {&ser, 0};
    pb_ostream_t stream = {VectorStream::Write, &str, SIZE_MAX, 0};
    const std::vector<uint64_t>* value_ptr = &arr;
    const int field_num = com_google_tuningfork_I64ArrTest_value_tag;
    ClearcutSerializer::writeCpuFreqs(&stream,
                                      &com_google_tuningfork_I64ArrTest_fields[field_num-1],
                                      (void *const*)&value_ptr);
    CheckArray(ser, arr, &I64ArrTest::mutable_value);
}

void CheckAnnotation(const std::vector<uint8_t>& ann) {
    Prong p;
    p.annotation_ = ann;
    std::vector<uint8_t> ser;
    VectorStream str {&ser, 0};
    pb_ostream_t stream = {VectorStream::Write, &str, SIZE_MAX, 0};
    const Prong* value_ptr = &p;
    const int field_num = com_google_tuningfork_BytesTest_value_tag;
    ClearcutSerializer::writeAnnotation(&stream,
                                        &com_google_tuningfork_BytesTest_fields[field_num-1],
                                        (void *const*)&value_ptr);
    CheckArray(ser, ann, &BytesTest::mutable_value);
}

void CheckFPs(const std::vector<uint8_t>& fps) {
    std::vector<uint8_t> ser;
    VectorStream str {&ser, 0};
    pb_ostream_t stream = {VectorStream::Write, &str, SIZE_MAX, 0};
    const ProtobufSerialization* value_ptr = &fps;
    const int field_num = com_google_tuningfork_BytesTest_value_tag;
    ClearcutSerializer::writeFidelityParams(&stream,
              &com_google_tuningfork_BytesTest_fields[field_num-1], (void *const*)&value_ptr);
    CheckArray(ser, fps, &BytesTest::mutable_value);
}

void CheckHistogramCountsValidated(const Histogram& h, const std::vector<uint32_t>& cnts) {
    std::vector<uint8_t> ser;
    VectorStream str {&ser, 0};
    pb_ostream_t stream = {VectorStream::Write, &str, SIZE_MAX, 0};
    const Histogram* value_ptr = &h;
    const int field_num = logs_proto_tuningfork_TuningForkHistogram_counts_tag;
    ClearcutSerializer::writeCountArray(&stream,
             &logs_proto_tuningfork_TuningForkHistogram_fields[field_num-1],
             (void *const*)&value_ptr);
    CheckArray(ser, cnts, &TuningForkHistogram::mutable_counts);
}

void CheckHistogramCounts(const std::vector<uint32_t>& cnts) {
    if (cnts.size()>2) {
        Histogram h(0,1,cnts.size()-2);
        h.SetCounts(cnts);
        CheckHistogramCountsValidated(h, cnts);
    }
    else {
        // Default histogram
        Histogram h;
        CheckHistogramCountsValidated(h, std::vector<uint32_t>(Histogram::kDefaultNumBuckets+2,0));
    }
}

void CheckDeviceInfo(const ExtraUploadInfo& info) {
    std::vector<uint8_t> ser;
    VectorStream str {&ser, 0};
    pb_ostream_t stream = {VectorStream::Write, &str, SIZE_MAX, 0};
    const ExtraUploadInfo* value_ptr = &info;
    const int field_num = logs_proto_tuningfork_TuningForkLogEvent_device_info_tag;
    ClearcutSerializer::writeDeviceInfo(&stream,
        &logs_proto_tuningfork_TuningForkLogEvent_fields[field_num-1], (void *const*)&value_ptr);
    TuningForkLogEvent evt;
    Deserialize(ser, evt);
    EXPECT_EQ(info.build_fingerprint, evt.device_info().build_fingerprint()) << "build_fingerprint";
    EXPECT_EQ(info.build_version_sdk, evt.device_info().build_version_sdk()) << "build_version_sdk";
    EXPECT_EQ(info.gl_es_version, evt.device_info().gl_es_version()) << "gl_es_version";
    EXPECT_EQ(info.total_memory_bytes, evt.device_info().total_memory_bytes())
      << "total_memory_bytes";
}

TEST(SerializationTest, String) {
    CheckString("");
    CheckString("Hello");
    CheckString("The quick brown fox jumped over the lazy dog"
                "The quick brown fox jumped over the lazy dog"
                "The quick brown fox jumped over the lazy dog");
}

TEST(SerializationTest, Int64Array) {
    CheckCpuFreqs({});
    CheckCpuFreqs({1,2,3});
    CheckCpuFreqs({0x7fffffffffffffff,0xffffffffffffffff,0x7fffffffffffffff,0x7fffffffffffffff});
}

TEST(SerializationTest, Annotation) {
    CheckAnnotation({});
    CheckAnnotation({0,1,2,3,4});
    CheckAnnotation({0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff});
}

TEST(SerializationTest, FidelityParams) {
    CheckFPs({});
    CheckFPs({0,1,2,3,4});
    CheckFPs({0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff});
}

TEST(SerializationTest, HistogramCounts) {
    CheckHistogramCounts({});
    CheckHistogramCounts({0,0,0});
    CheckHistogramCounts({2,3,4,5,6,7,8,9});
}

TEST(SerializationTest, DeviceInfo) {
    CheckDeviceInfo({"expt", "sess", 2387, 349587, "fing", "version", {1,2,3}, "packname"});
}

} // namespace serialization_test
