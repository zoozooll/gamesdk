
#include "tuningfork/tuningfork_extra.h"
#include "tuningfork_internal.h"
#include "tuningfork/protobuf_util.h"

#include <cinttypes>
#include <dlfcn.h>
#include <memory>
#include <vector>
#include <cstdlib>
#include <sstream>
#include <thread>
#include <fstream>
#include <mutex>
#include <sys/stat.h>
#include <errno.h>

#define LOG_TAG "TuningFork"
#include "Log.h"
#include "swappy/swappy_extra.h"

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <jni.h>

namespace tf = tuningfork;

namespace tuningfork {

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
public:
    SwappyTuningFork(const CProtobufSerialization& settings_ser, JNIEnv* env, jobject activity,
                     VoidCallback cbk, const char* libName)
        : swappy_(libName), trace_({}), frame_callback_(cbk) {
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
    static void swappyStartFrameCallback(void* userPtr, int /*currentFrame*/,
                                         long /*currentFrameTimeStampMs*/) {
        SwappyTuningFork* _this = (SwappyTuningFork*)userPtr;
        _this->frame_callback_();
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

    static bool Init(const CProtobufSerialization* settings, JNIEnv* env,
                     jobject activity, const char* libName, void (*frame_callback)()) {
        s_instance_ = std::unique_ptr<SwappyTuningFork>(
            new SwappyTuningFork(*settings, env, activity, frame_callback, libName));
        return s_instance_->valid();
    }
};

std::unique_ptr<SwappyTuningFork> SwappyTuningFork::s_instance_;

namespace apk_utils {

    // Get an asset from this APK's asset directory.
    // Returns NULL if the asset could not be found.
    // Asset_close must be called once the asset is no longer needed.
    AAsset* GetAsset(JNIEnv* env, jobject activity, const char* name) {
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

    // Gets the serialized settings from the APK.
    // Returns false if there was an error.
    bool GetSettingsSerialization(JNIEnv* env, jobject activity,
                                         CProtobufSerialization& settings_ser) {
        auto asset = GetAsset(env, activity, "tuningfork/tuningfork_settings.bin");
        if (asset == nullptr )
            return false;
        ALOGI("Got settings from tuningfork/tuningfork_settings.bin");
        // Get serialized settings from assets
        uint64_t size = AAsset_getLength64(asset);
        settings_ser.bytes = (uint8_t*)::malloc(size);
        memcpy(settings_ser.bytes, AAsset_getBuffer(asset), size);
        settings_ser.size = size;
        settings_ser.dealloc = ::free;
        AAsset_close(asset);
        return true;
    }

    // Gets the serialized fidelity params from the APK.
    // Call this function once with fps_ser=NULL to get the count of files present,
    // then allocate an array of CProtobufSerializations and pass this as fps_ser
    // to a second call.
    void GetFidelityParamsSerialization(JNIEnv* env, jobject activity,
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

    // Get the app's version code. Also fills packageNameStr with the package name
    //  if it is non-null.
    int GetVersionCode(JNIEnv *env, jobject context, std::string* packageNameStr) {
        jstring packageName;
        jobject packageManagerObj;
        jobject packageInfoObj;
        jclass contextClass =  env->GetObjectClass( context);
        jmethodID getPackageNameMid = env->GetMethodID( contextClass, "getPackageName",
            "()Ljava/lang/String;");
        jmethodID getPackageManager =  env->GetMethodID( contextClass, "getPackageManager",
            "()Landroid/content/pm/PackageManager;");

        jclass packageManagerClass = env->FindClass("android/content/pm/PackageManager");
        jmethodID getPackageInfo = env->GetMethodID( packageManagerClass, "getPackageInfo",
            "(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;");

        jclass packageInfoClass = env->FindClass("android/content/pm/PackageInfo");
        jfieldID versionCodeFid = env->GetFieldID( packageInfoClass, "versionCode", "I");

        packageName =  (jstring)env->CallObjectMethod(context, getPackageNameMid);

        if (packageNameStr != nullptr) {
            // Fill packageNameStr with the package name
            const char* packageName_cstr = env->GetStringUTFChars(packageName, NULL);
            *packageNameStr = std::string(packageName_cstr);
            env->ReleaseStringUTFChars(packageName, packageName_cstr);
        }
        // Get version code from package info
        packageManagerObj = env->CallObjectMethod(context, getPackageManager);
        packageInfoObj = env->CallObjectMethod(packageManagerObj,getPackageInfo,
                                               packageName, 0x0);
        int versionCode = env->GetIntField( packageInfoObj, versionCodeFid);
        return versionCode;
    }

} // namespace apk_utils

namespace file_utils {

    // Creates the directory if it does not exist. Returns true if the directory
    //  already existed or could be created.
    bool CheckAndCreateDir(const std::string& path) {
        struct stat sb;
        int32_t res = stat(path.c_str(), &sb);
        if (0 == res && sb.st_mode & S_IFDIR) {
            ALOGV("Directory %s already exists", path.c_str());
            return true;
        } else if (ENOENT == errno) {
            ALOGI("Creating directory %s", path.c_str());
            res = mkdir(path.c_str(), 0770);
            if(res!=0)
                ALOGW("Error creating directory %s: %d", path.c_str(), res);
            return res==0;
        }
        return false;
    }

    // Get the name of the tuning fork save file. Returns true if the directory
    //  for the file exists and false on error.
    bool GetSavedFileName(JNIEnv* env, jobject activity, std::string& name) {
        jclass activityClass = env->FindClass( "android/app/NativeActivity" );
        jmethodID getCacheDir = env->GetMethodID( activityClass, "getCacheDir",
            "()Ljava/io/File;" );
        jobject cache_dir = env->CallObjectMethod( activity, getCacheDir );

        jclass fileClass = env->FindClass( "java/io/File" );
        jmethodID getPath = env->GetMethodID( fileClass, "getPath", "()Ljava/lang/String;" );
        jstring path_string = (jstring)env->CallObjectMethod( cache_dir, getPath );

        const char *path_chars = env->GetStringUTFChars( path_string, NULL );
        std::string temp_folder( path_chars );
        env->ReleaseStringUTFChars( path_string, path_chars );

        // Create tuningfork/version folder if it doesn't exist
        std::stringstream tf_path_str;
        tf_path_str << temp_folder << "/tuningfork";
        if (!CheckAndCreateDir(tf_path_str.str())) {
            return false;
        }
        tf_path_str << "/V" << apk_utils::GetVersionCode(env, activity);
        if (!CheckAndCreateDir(tf_path_str.str())) {
            return false;
        }
        tf_path_str << "/saved_fp.bin";
        name = tf_path_str.str();
        return true;
    }

    // Get a previously save fidelity param serialization.
    bool GetSavedFidelityParams(JNIEnv* env, jobject activity, CProtobufSerialization* params) {
        std::string save_filename;
        if (GetSavedFileName(env, activity, save_filename)) {
            std::ifstream save_file(save_filename, std::ios::binary);
            if (save_file.good()) {
                save_file.seekg(0, std::ios::end);
                params->size = save_file.tellg();
                params->bytes = (uint8_t*)::malloc(params->size);
                params->dealloc = ::free;
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
    bool SaveFidelityParams(JNIEnv* env, jobject activity, const CProtobufSerialization* params) {
        std::string save_filename;
        if (GetSavedFileName(env, activity, save_filename)) {
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
    bool SavedFidelityParamsFileExists(JNIEnv* env, jobject activity) {
        std::string save_filename;
        if (GetSavedFileName(env, activity, save_filename)) {
            struct stat buffer;
            return (stat(save_filename.c_str(), &buffer)==0);
        }
        return false;
    }

} // namespace file_utils

// Download FPs on a separate thread
void StartFidelityParamDownloadThread(JNIEnv* env, jobject activity,
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
    auto newActivity = env->NewGlobalRef(activity);
    fpThread = std::thread([=](CProtobufSerialization defaultParams) {
        CProtobufSerialization params = {};
        int waitTimeMs = initialTimeoutMs;
        bool first_time = true;
        JNIEnv *newEnv;
        if (vm->AttachCurrentThread(&newEnv, NULL) == 0) {
            while (true) {
                if (TuningFork_getFidelityParameters(&defaultParams,
                                                     &params, waitTimeMs)) {
                    ALOGI("Got fidelity params from server");
                    file_utils::SaveFidelityParams(newEnv, newActivity, &params);
                    tf::CProtobufSerialization_Free(&defaultParams);
                    fidelity_params_callback(&params);
                    tf::CProtobufSerialization_Free(&params);
                    break;
                } else {
                    ALOGI("Could not get fidelity params from server");
                    if (first_time) {
                        fidelity_params_callback(&defaultParams);
                        first_time = false;
                    }
                    if (waitTimeMs > ultimateTimeoutMs) {
                        ALOGW("Not waiting any longer for fidelity params");
                        tf::CProtobufSerialization_Free(&defaultParams);
                        break;
                    }
                    waitTimeMs *= 2; // back off
                }
            }
            newEnv->DeleteGlobalRef(newActivity);
            vm->DetachCurrentThread();
        }
    }, defaultParams);
}

} // namespace tuningfork

extern "C" {

using namespace tuningfork;

bool TuningFork_findSettingsInAPK(JNIEnv* env, jobject activity,
                                  CProtobufSerialization* settings_ser) {
    if(settings_ser) {
        return apk_utils::GetSettingsSerialization(env, activity, *settings_ser);
    } else {
        return false;
    }
}
void TuningFork_findFidelityParamsInAPK(JNIEnv* env, jobject activity,
                                        CProtobufSerialization* fps, int* fp_count) {
    apk_utils::GetFidelityParamsSerialization(env, activity, fps, fp_count);
}

bool TuningFork_initWithSwappy(const CProtobufSerialization* settings, JNIEnv* env,
                               jobject activity, const char* libraryName,
                               VoidCallback frame_callback) {
    return SwappyTuningFork::Init(settings, env, activity, libraryName, frame_callback);
}

void TuningFork_setUploadCallback(void(*cbk)(const CProtobufSerialization*)) {
    tuningfork::SetUploadCallback(cbk);
}

TFErrorCode TuningFork_initFromAssetsWithSwappy(JNIEnv* env, jobject activity,
                                                const char* libraryName,
                                                VoidCallback frame_callback,
                                                int fpFileNum,
                                                ProtoCallback fidelity_params_callback,
                                                int initialTimeoutMs, int ultimateTimeoutMs) {
    CProtobufSerialization ser;
    if (!TuningFork_findSettingsInAPK(env, activity, &ser))
        return TFERROR_NO_SETTINGS;
    if (!TuningFork_initWithSwappy(&ser, env, activity, libraryName, frame_callback))
        return TFERROR_NO_SWAPPY;
    CProtobufSerialization defaultParams = {};
    // Special meaning for negative fpFileNum: don't load saved params, overwrite them instead
    bool resetSavedFPs = fpFileNum<0;
    fpFileNum = abs(fpFileNum);
    // Use the saved params as default, if they exist
    if (!resetSavedFPs && file_utils::SavedFidelityParamsFileExists(env, activity)) {
        file_utils::GetSavedFidelityParams(env, activity, &defaultParams);
    } else {
        int nfps=0;
        TuningFork_findFidelityParamsInAPK(env, activity, NULL, &nfps);
        if (nfps>0) {
            std::vector<CProtobufSerialization> fps(nfps);
            TuningFork_findFidelityParamsInAPK(env, activity, fps.data(), &nfps);
            int chosen = fpFileNum - 1; // File indices start at 1
            for (int i=0;i<nfps;++i) {
                if (i==chosen) {
                    defaultParams = fps[i];
                } else {
                    tf::CProtobufSerialization_Free(&fps[i]);
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
        file_utils::SaveFidelityParams(env, activity, &defaultParams);
    }
    StartFidelityParamDownloadThread(env, activity, defaultParams, fidelity_params_callback,
        initialTimeoutMs, ultimateTimeoutMs);
    return TFERROR_OK;
}

} // extern "C"
