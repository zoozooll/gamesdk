
#include "tuningfork/tuningfork_extra.h"
#include "tuningfork_internal.h"

#include <cinttypes>
#include <dlfcn.h>
#include <memory>
#include <vector>
#include <cstdlib>
#include <sstream>

#define LOG_TAG "TuningFork"
#include "Log.h"
#include "swappy/swappy_extra.h"

#include <android/asset_manager.h> 
#include <android/asset_manager_jni.h>

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
                if(inject_tracer_) return;
            }
        }
        ALOGW("Couldn't find Swappy_injectTracer");
        lib_ = nullptr;
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
    void (*annotation_callback_)();
    TFTraceHandle waitTraceHandle_ = 0;
    TFTraceHandle swapTraceHandle_ = 0;
public:
    SwappyTuningFork(const CProtobufSerialization& settings_ser, JNIEnv* env, jobject activity,
                     void (*cbk)(), const char* libName)
        : swappy_(libName), trace_({}), annotation_callback_(cbk) {
        trace_.startFrame = swappyStartFrameCallback;
        trace_.preWait =  swappyPreWaitCallback;
        trace_.postWait = swappyPostWaitCallback;
        trace_.preSwapBuffers = swappyPreSwapBuffersCallback;
        trace_.postSwapBuffers = swappyPostSwapBuffersCallback;
        trace_.userData = this;
        if(swappy_.valid()) {
            TuningFork_init(&settings_ser, env, activity);
            swappy_.injectTracer(&trace_);
        }
    }
    bool valid() const { return swappy_.valid(); }

    // Swappy trace callbacks
    static void swappyStartFrameCallback(void* userPtr, int /*currentFrame*/, long /*currentFrameTimeStampMs*/) {
        SwappyTuningFork* _this = (SwappyTuningFork*)userPtr;
        _this->annotation_callback_();
        TuningFork_frameTick(TFTICK_SYSCPU);
    }
    static void swappyPreWaitCallback(void* userPtr) {
        SwappyTuningFork* _this = (SwappyTuningFork*)userPtr;
        _this->waitTraceHandle_ = TuningFork_startTrace(TFTICK_WAIT_TIME);
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
        _this->swapTraceHandle_ = TuningFork_startTrace(TFTICK_SWAP_TIME);
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

    static AAsset* GetAsset(JNIEnv* env, jobject activity, const char* name) {
        jclass cls = env->FindClass("android/content/Context");
        jmethodID get_assets = env->GetMethodID(cls, "getAssets",
                                                "()Landroid/content/res/AssetManager;");
        if(get_assets==nullptr) {
            ALOGE("No Context.getAssets() method");
            return nullptr;
        }
        auto javaMgr = env->CallObjectMethod(activity, get_assets);
        if (javaMgr == nullptr) {
            ALOGE("No java asset manager");
            return nullptr;
        }
        AAssetManager* mgr = AAssetManager_fromJava(env, javaMgr);
        if (mgr == nullptr) {
            ALOGE("No asset manager");
            return nullptr;
        }
        AAsset* asset = AAssetManager_open(mgr, name,
                                           AASSET_MODE_BUFFER);
        if (asset == nullptr) {
            ALOGW("Can't find %s in APK", name);
            return nullptr;
        }
        return asset;
    }
    static bool GetSettingsSerialization(JNIEnv* env, jobject activity,
                                         CProtobufSerialization& settings_ser) {
        auto asset = GetAsset(env, activity, "tuningfork/tuningfork_settings.bin");
        if (asset == nullptr )
            return false;
        ALOGI("Using settings from tuningfork/tuningfork_settings.bin");
        // Get serialized settings from assets
        uint64_t size = AAsset_getLength64(asset);
        settings_ser.bytes = (uint8_t*)::malloc(size);
        memcpy(settings_ser.bytes, AAsset_getBuffer(asset), size);
        settings_ser.size = size;
        settings_ser.dealloc = ::free;
        AAsset_close(asset);
        return true;
    }
    static void GetFidelityParamsSerialization(JNIEnv* env, jobject activity,
                                               CProtobufSerialization* fps_ser,
                                               int* fp_count) {
        std::vector<AAsset*> fps;
        for( int i=1; i<16; ++i ) {
            std::stringstream name;
            name << "tuningfork/dev_tuningfork_fidelityparams_" << i << ".bin";
            auto fp = GetAsset(env, activity, name.str().c_str());
            if ( fp == nullptr ) break;
            fps.push_back(fp);
        }
        *fp_count = fps.size();
        if( fps_ser==nullptr )
            return;
        for(int i=0; i<*fp_count; ++i) {
            // Get serialized FidelityParams from assets
            AAsset* asset = fps[i];
            CProtobufSerialization& fp_ser = fps_ser[i];
            uint64_t size = AAsset_getLength64(asset);
            fp_ser.bytes = (uint8_t*)::malloc(size);
            memcpy(fp_ser.bytes, AAsset_getBuffer(asset), size);
            fp_ser.size = size;
            fp_ser.dealloc = ::free;
            AAsset_close(asset);
        }
    }
    static bool Init(const CProtobufSerialization* settings, JNIEnv* env,
                     jobject activity, const char* libName, void (*annotation_callback)()) {
        s_instance_ = std::unique_ptr<SwappyTuningFork>(
            new SwappyTuningFork(*settings, env, activity, annotation_callback, libName));
        return s_instance_->valid();
    }
};

std::unique_ptr<SwappyTuningFork> SwappyTuningFork::s_instance_;

} // anonymous namespace

extern "C"
bool TuningFork_findSettingsInAPK(JNIEnv* env, jobject activity,
                                  CProtobufSerialization* settings_ser) {
    if(settings_ser)
        return SwappyTuningFork::GetSettingsSerialization(env, activity, *settings_ser);
    else
        return false;
}
extern "C"
void TuningFork_findFidelityParamsInAPK(JNIEnv* env, jobject activity,
                                        CProtobufSerialization* fps, int* fp_count) {
    SwappyTuningFork::GetFidelityParamsSerialization(env, activity, fps, fp_count);
}

extern "C"
bool TuningFork_initWithSwappy(const CProtobufSerialization* settings, JNIEnv* env,
                               jobject activity, const char* libraryName,
                               void (*annotation_callback)()) {
    return SwappyTuningFork::Init(settings, env, activity, libraryName, annotation_callback);
}

extern "C"
void TuningFork_setUploadCallback(void(*cbk)(const CProtobufSerialization*)) {
    tuningfork::SetUploadCallback(cbk);
}
