
#include "tuningfork/tuningfork_extra.h"
#include "tuningfork/protobuf_util.h"
#include "tuningfork_internal.h"
#include "tuningfork_utils.h"

#include <cinttypes>
#include <dlfcn.h>
#include <memory>
#include <vector>
#include <cstdlib>
#include <sstream>
#include <thread>
#include <fstream>
#include <mutex>

#define LOG_TAG "TuningFork"
#include "Log.h"
#include "swappy/swappy_extra.h"

#include <android/asset_manager_jni.h>
#include <jni.h>

#include "tuningfork/protobuf_nano_util.h"
#include "pb_decode.h"
#include "nano/tuningfork.pb.h"
using PBSettings = com_google_tuningfork_Settings;

using namespace tuningfork;

namespace {

using PFN_Swappy_initTracer = void (*)(const SwappyTracer* tracer);

constexpr TFInstrumentKey TFTICK_WAIT_TIME = 2;
constexpr TFInstrumentKey TFTICK_SWAP_TIME = 3;

class DynamicSwappy {
    typedef void* Handle;
    Handle lib_;
    PFN_Swappy_initTracer inject_tracer_;
public:
    DynamicSwappy(const char* libraryName) {
        static char defaultLibNames[][20] = {"libgamesdk.so", "libswappy.so", "libunity.so"};
        std::vector<const char*> libNames = {
            libraryName, NULL, defaultLibNames[0], defaultLibNames[1], defaultLibNames[2]};
        for(auto libName: libNames) {
            lib_ = dlopen(libName, RTLD_NOW);
            if( lib_ ) {
                inject_tracer_ = (PFN_Swappy_initTracer)dlsym(lib_, "Swappy_injectTracer");
                if(inject_tracer_) {
                    return;
                } else {
                    dlclose(lib_);
                }
            }
        }
        ALOGW("Couldn't find Swappy_injectTracer");
        lib_ = nullptr;
    }
    ~DynamicSwappy() {
        if(lib_) dlclose(lib_);
    }
    void injectTracer(const SwappyTracer* tracer) const {
        if(inject_tracer_)
            inject_tracer_(tracer);
    }
    bool valid() const { return lib_ != nullptr; }
};

class SwappyTuningFork {
    DynamicSwappy swappy_;
    SwappyTracer trace_;
    VoidCallback frame_callback_;
    TFTraceHandle waitTraceHandle_ = 0;
    TFTraceHandle swapTraceHandle_ = 0;
    TFErrorCode tfInitError;
public:
    SwappyTuningFork(const TFSettings& settings, JNIEnv* env, jobject context,
                     VoidCallback cbk, const char* libName)
        : swappy_(libName), trace_({}), frame_callback_(cbk), tfInitError(TFERROR_OK) {
        trace_.startFrame = swappyStartFrameCallback;
        trace_.preWait =  swappyPreWaitCallback;
        trace_.postWait = swappyPostWaitCallback;
        trace_.preSwapBuffers = swappyPreSwapBuffersCallback;
        trace_.postSwapBuffers = swappyPostSwapBuffersCallback;
        trace_.userData = this;
        if(swappy_.valid()) {
            tfInitError = TuningFork_init(&settings, env, context);
            if (tfInitError==TFERROR_OK)
                swappy_.injectTracer(&trace_);
        }
    }
    bool valid() const { return swappy_.valid() && (tfInitError==TFERROR_OK); }

    // Swappy trace callbacks
    static void swappyStartFrameCallback(void* userPtr, int /*currentFrame*/,
                                         long /*currentFrameTimeStampMs*/) {
        SwappyTuningFork* _this = (SwappyTuningFork*)userPtr;
        _this->frame_callback_();
        TuningFork_frameTick(TFTICK_SYSCPU);
    }
    static void swappyPreWaitCallback(void* userPtr) {
        SwappyTuningFork* _this = (SwappyTuningFork*)userPtr;
        TuningFork_startTrace(TFTICK_WAIT_TIME, &_this->waitTraceHandle_);
    }
    static void swappyPostWaitCallback(void* userPtr) {
        SwappyTuningFork *_this = (SwappyTuningFork *) userPtr;
        if (_this->waitTraceHandle_) {
            TuningFork_endTrace(_this->waitTraceHandle_);
            _this->waitTraceHandle_ = 0;
        }
        TuningFork_frameTick(TFTICK_SYSGPU);
    }
    static void swappyPreSwapBuffersCallback(void* userPtr) {
        SwappyTuningFork* _this = (SwappyTuningFork*)userPtr;
        TuningFork_startTrace(TFTICK_SWAP_TIME, &_this->swapTraceHandle_);
    }
    static void swappyPostSwapBuffersCallback(void* userPtr, long /*desiredPresentationTimeMs*/) {
        SwappyTuningFork *_this = (SwappyTuningFork *) userPtr;
        if (_this->swapTraceHandle_) {
            TuningFork_endTrace(_this->swapTraceHandle_);
            _this->swapTraceHandle_ = 0;
        }
    }
    // Static methods
    static std::unique_ptr<SwappyTuningFork> s_instance_;

    static bool Init(const TFSettings* settings, JNIEnv* env,
                     jobject context, const char* libName, void (*frame_callback)()) {
        s_instance_ = std::unique_ptr<SwappyTuningFork>(
            new SwappyTuningFork(*settings, env, context, frame_callback, libName));
        return s_instance_->valid();
    }
};

std::unique_ptr<SwappyTuningFork> SwappyTuningFork::s_instance_;

// Gets the serialized settings from the APK.
// Returns false if there was an error.
bool GetSettingsSerialization(JNIEnv* env, jobject context,
                                        CProtobufSerialization& settings_ser) {
    auto asset = apk_utils::GetAsset(env, context, "tuningfork/tuningfork_settings.bin");
    if (asset == nullptr )
        return false;
    ALOGI("Got settings from tuningfork/tuningfork_settings.bin");
    // Get serialized settings from assets
    uint64_t size = AAsset_getLength64(asset);
    settings_ser.bytes = (uint8_t*)::malloc(size);
    memcpy(settings_ser.bytes, AAsset_getBuffer(asset), size);
    settings_ser.size = size;
    settings_ser.dealloc = CProtobufSerialization_Dealloc;
    AAsset_close(asset);
    return true;
}

// Gets the serialized fidelity params from the APK.
// Call this function once with fps_ser=NULL to get the count of files present,
// then allocate an array of CProtobufSerializations and pass this as fps_ser
// to a second call.
TFErrorCode GetFidelityParamsSerialization(JNIEnv* env, jobject context,
                                            CProtobufSerialization* fps_ser,
                                            int* fp_count) {
    std::vector<AAsset*> fps;
    for( int i=1; i<16; ++i ) {
        std::stringstream name;
        name << "tuningfork/dev_tuningfork_fidelityparams_" << i << ".bin";
        auto fp = apk_utils::GetAsset(env, context, name.str().c_str());
        if ( fp == nullptr ) break;
        fps.push_back(fp);
    }
    *fp_count = fps.size();
    if( fps_ser==nullptr )
        return TFERROR_OK;
    for(int i=0; i<*fp_count; ++i) {
        // Get serialized FidelityParams from assets
        AAsset* asset = fps[i];
        CProtobufSerialization& fp_ser = fps_ser[i];
        uint64_t size = AAsset_getLength64(asset);
        fp_ser.bytes = (uint8_t*)::malloc(size);
        memcpy(fp_ser.bytes, AAsset_getBuffer(asset), size);
        fp_ser.size = size;
        fp_ser.dealloc = CProtobufSerialization_Dealloc;
        AAsset_close(asset);
    }
    return TFERROR_OK;
}

// Get the name of the tuning fork save file. Returns true if the directory
//  for the file exists and false on error.
bool GetSavedFileName(JNIEnv* env, jobject context, std::string& name) {

    // Create tuningfork/version folder if it doesn't exist
    std::stringstream tf_path_str;
    tf_path_str << file_utils::GetAppCacheDir(env, context) << "/tuningfork";
    if (!file_utils::CheckAndCreateDir(tf_path_str.str())) {
        return false;
    }
    tf_path_str << "/V" << apk_utils::GetVersionCode(env, context);
    if (!file_utils::CheckAndCreateDir(tf_path_str.str())) {
        return false;
    }
    tf_path_str << "/saved_fp.bin";
    name = tf_path_str.str();
    return true;
}

// Get a previously save fidelity param serialization.
bool GetSavedFidelityParams(JNIEnv* env, jobject context, CProtobufSerialization* params) {
    std::string save_filename;
    if (GetSavedFileName(env, context, save_filename)) {
        std::ifstream save_file(save_filename, std::ios::binary);
        if (save_file.good()) {
            save_file.seekg(0, std::ios::end);
            params->size = save_file.tellg();
            params->bytes = (uint8_t*)::malloc(params->size);
            params->dealloc = CProtobufSerialization_Dealloc;
            save_file.seekg(0, std::ios::beg);
            save_file.read((char*)params->bytes, params->size);
            ALOGI("Loaded fps from %s (%zu bytes)", save_filename.c_str(), params->size);
            return true;
        }
        ALOGI("Couldn't load fps from %s", save_filename.c_str());
    }
    return false;
}

// Save fidelity params to the save file.
bool SaveFidelityParams(JNIEnv* env, jobject context, const CProtobufSerialization* params) {
    std::string save_filename;
    if (GetSavedFileName(env, context, save_filename)) {
        std::ofstream save_file(save_filename, std::ios::binary);
        if (save_file.good()) {
            save_file.write((const char*)params->bytes, params->size);
            ALOGI("Saved fps to %s (%zu bytes)", save_filename.c_str(), params->size);
            return true;
        }
        ALOGI("Couldn't save fps to %s", save_filename.c_str());
    }
    return false;
}

// Check if we have saved fidelity params.
bool SavedFidelityParamsFileExists(JNIEnv* env, jobject context) {
    std::string save_filename;
    if (GetSavedFileName(env, context, save_filename)) {
        return file_utils::FileExists(save_filename);
    }
    return false;
}

// Download FPs on a separate thread
void StartFidelityParamDownloadThread(JNIEnv* env, jobject context,
                                      const CProtobufSerialization& defaultParams,
                                      ProtoCallback fidelity_params_callback,
                                      int initialTimeoutMs, int ultimateTimeoutMs) {
    static std::mutex threadMutex;
    std::lock_guard<std::mutex> lock(threadMutex);
    static std::thread fpThread;
    if (fpThread.joinable()) {
        ALOGW("Fidelity param download thread already started");
        return;
    }
    JavaVM *vm;
    env->GetJavaVM(&vm);
    jobject newContextRef = env->NewGlobalRef(context);
    fpThread = std::thread([=](CProtobufSerialization defaultParams) {
        CProtobufSerialization params = {};
        int waitTimeMs = initialTimeoutMs;
        bool first_time = true;
        JNIEnv *newEnv;
        if (vm->AttachCurrentThread(&newEnv, NULL) == 0) {
            while (true) {
                auto err = TuningFork_getFidelityParameters(&defaultParams,
                                                            &params, waitTimeMs);
                if (err==TFERROR_OK) {
                    ALOGI("Got fidelity params from server");
                    SaveFidelityParams(newEnv, newContextRef, &params);
                    CProtobufSerialization_Free(&defaultParams);
                    fidelity_params_callback(&params);
                    CProtobufSerialization_Free(&params);
                    break;
                } else {
                    ALOGI("Could not get fidelity params from server : err = %d", err);
                    if (first_time) {
                        fidelity_params_callback(&defaultParams);
                        first_time = false;
                    }
                    if (waitTimeMs > ultimateTimeoutMs) {
                        ALOGW("Not waiting any longer for fidelity params");
                        CProtobufSerialization_Free(&defaultParams);
                        break;
                    }
                    waitTimeMs *= 2; // back off
                }
            }
            newEnv->DeleteGlobalRef(newContextRef);
            vm->DetachCurrentThread();
        }
    }, defaultParams);
}

template<typename T>
void push_back(T*& x, uint32_t& n, const T& val) {
    if (x) {
        x = (T*)realloc(x, (n+1)*sizeof(T));
    } else {
        x = (T*)malloc(sizeof(T));
    }
    x[n] = val;
    ++n;
}
bool decodeAnnotationEnumSizes(pb_istream_t* stream, const pb_field_t *field, void** arg) {
    TFSettings* settings = static_cast<TFSettings*>(*arg);
    uint64_t a;
    pb_decode_varint(stream, &a);
    push_back(settings->aggregation_strategy.annotation_enum_size,
              settings->aggregation_strategy.n_annotation_enum_size, (uint32_t)a);
    return true;
}
bool decodeHistograms(pb_istream_t* stream, const pb_field_t *field, void** arg) {
    TFSettings* settings = static_cast<TFSettings*>(*arg);
    com_google_tuningfork_Settings_Histogram hist;
    pb_decode(stream, com_google_tuningfork_Settings_Histogram_fields, &hist);
    push_back(settings->histograms, settings->n_histograms,
              {hist.instrument_key, hist.bucket_min, hist.bucket_max, hist.n_buckets});
    return true;
}

} // anonymous namespace

extern "C" {

void TFSettings_Dealloc(TFSettings* s) {
    if(s->histograms) {
        free(s->histograms);
        s->histograms = nullptr;
        s->n_histograms = 0;
    }
    if(s->aggregation_strategy.annotation_enum_size) {
        free(s->aggregation_strategy.annotation_enum_size);
        s->aggregation_strategy.annotation_enum_size = nullptr;
        s->aggregation_strategy.n_annotation_enum_size = 0;
    }
}

TFErrorCode TuningFork_deserializeSettings(const CProtobufSerialization* settings_ser, TFSettings* settings) {
    settings->n_histograms = 0;
    settings->histograms = nullptr;
    settings->aggregation_strategy.n_annotation_enum_size = 0;
    settings->aggregation_strategy.annotation_enum_size = nullptr;
    settings->dealloc = TFSettings_Dealloc;
    PBSettings pbsettings = com_google_tuningfork_Settings_init_zero;
    pbsettings.aggregation_strategy.annotation_enum_size.funcs.decode = decodeAnnotationEnumSizes;
    pbsettings.aggregation_strategy.annotation_enum_size.arg = settings;
    pbsettings.histograms.funcs.decode = decodeHistograms;
    pbsettings.histograms.arg = settings;
    ByteStream str {settings_ser->bytes, settings_ser->size, 0};
    pb_istream_t stream = {ByteStream::Read, &str, settings_ser->size};
    pb_decode(&stream, com_google_tuningfork_Settings_fields, &pbsettings);
    if(pbsettings.aggregation_strategy.method
          ==com_google_tuningfork_Settings_AggregationStrategy_Submission_TICK_BASED)
        settings->aggregation_strategy.method = TFAggregationStrategy::TICK_BASED;
    else
        settings->aggregation_strategy.method = TFAggregationStrategy::TIME_BASED;
    settings->aggregation_strategy.intervalms_or_count
      = pbsettings.aggregation_strategy.intervalms_or_count;
    settings->aggregation_strategy.max_instrumentation_keys
      = pbsettings.aggregation_strategy.max_instrumentation_keys;
    return TFERROR_OK;
}

TFErrorCode TuningFork_findSettingsInApk(JNIEnv* env, jobject context,
                                  TFSettings* settings) {
    if (settings) {
        CProtobufSerialization settings_ser;
        if (GetSettingsSerialization(env, context, settings_ser)) {
            auto r = TuningFork_deserializeSettings(&settings_ser, settings);
            CProtobufSerialization_Free(&settings_ser);
            return r;
        }
        else {
            return TFERROR_NO_SETTINGS;
        }
    } else {
        return TFERROR_BAD_PARAMETER;
    }
}
TFErrorCode TuningFork_findFidelityParamsInApk(JNIEnv* env, jobject context,
                                        CProtobufSerialization* fps, int* fp_count) {
    return GetFidelityParamsSerialization(env, context, fps, fp_count);
}

TFErrorCode TuningFork_initWithSwappy(const TFSettings* settings, JNIEnv* env,
                               jobject context, const char* libraryName,
                               VoidCallback frame_callback) {
    if (SwappyTuningFork::Init(settings, env, context, libraryName, frame_callback))
        return TFERROR_OK;
    else
        return TFERROR_NO_SWAPPY;
}

TFErrorCode TuningFork_setUploadCallback(void(*cbk)(const CProtobufSerialization*)) {
    return tuningfork::SetUploadCallback(cbk);
}

TFErrorCode TuningFork_initFromAssetsWithSwappy(JNIEnv* env, jobject context,
                                                const char* libraryName,
                                                VoidCallback frame_callback,
                                                int fpFileNum,
                                                ProtoCallback fidelity_params_callback,
                                                int initialTimeoutMs, int ultimateTimeoutMs) {
    TFSettings settings;
    auto err = TuningFork_findSettingsInApk(env, context, &settings);
    if (err!=TFERROR_OK)
        return err;
    err = TuningFork_initWithSwappy(&settings, env, context, libraryName, frame_callback);
    settings.dealloc(&settings);
    if (err!=TFERROR_OK)
        return err;
    CProtobufSerialization defaultParams = {};
    // Special meaning for negative fpFileNum: don't load saved params, overwrite them instead
    bool resetSavedFPs = fpFileNum<0;
    fpFileNum = abs(fpFileNum);
    // Use the saved params as default, if they exist
    if (!resetSavedFPs && SavedFidelityParamsFileExists(env, context)) {
        GetSavedFidelityParams(env, context, &defaultParams);
    } else {
        int nfps=0;
        TuningFork_findFidelityParamsInApk(env, context, NULL, &nfps);
        if (nfps>0) {
            std::vector<CProtobufSerialization> fps(nfps);
            TuningFork_findFidelityParamsInApk(env, context, fps.data(), &nfps);
            int chosen = fpFileNum - 1; // File indices start at 1
            for (int i=0;i<nfps;++i) {
                if (i==chosen) {
                    defaultParams = fps[i];
                } else {
                    CProtobufSerialization_Free(&fps[i]);
                }
            }
            if (chosen>=0 && chosen<nfps) {
                ALOGI("Using params from dev_tuningfork_fidelityparams_%d.bin as default",
                    fpFileNum);
            } else {
                return TFERROR_INVALID_DEFAULT_FIDELITY_PARAMS;
            }
        } else {
            return TFERROR_NO_FIDELITY_PARAMS;
        }
        // Save the default params
        SaveFidelityParams(env, context, &defaultParams);
    }
    StartFidelityParamDownloadThread(env, context, defaultParams, fidelity_params_callback,
        initialTimeoutMs, ultimateTimeoutMs);
    return TFERROR_OK;
}

} // extern "C"
