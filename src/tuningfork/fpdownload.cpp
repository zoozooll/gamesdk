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

#include "tuningfork_internal.h"

#include <sstream>
#include <string>

#define LOG_TAG "FPDownload"
#include "Log.h"

#include "jni_helper.h"
#include "../../third_party/json11/json11.hpp"
#include "modp_b64.h"

namespace tuningfork {

const char url_rpcname[] = ":generateTuningParameters";

std::string GetPartialURL(const ExtraUploadInfo& requestInfo) {
    std::stringstream str;
    str << "applications/"<< requestInfo.apk_package_name<<"/apks/";
    str << requestInfo.apk_version_code;
    return str.str();
}

std::string RequestJson(const ExtraUploadInfo& requestInfo) {
    using namespace json11;
    Json gles_version = Json::object {
        {"major", static_cast<int>(requestInfo.gl_es_version>>16)},
        {"minor", static_cast<int>(requestInfo.gl_es_version&0xffff)}};
    std::vector<double> freqs(requestInfo.cpu_max_freq_hz.begin(),
                              requestInfo.cpu_max_freq_hz.end());
    Json device_spec = Json::object {
        {"fingerprint", requestInfo.build_fingerprint},
        {"total_memory_bytes", static_cast<double>(requestInfo.total_memory_bytes)},
        {"build_version", requestInfo.build_version_sdk},
        {"gles_version", gles_version},
        {"cpu_core_freqs_hz", freqs}};
    Json request = Json::object {
        {"name", GetPartialURL(requestInfo)},
        {"device_spec", device_spec}};
    auto result = request.dump();
    ALOGI("Request body: %s", result.c_str());
    return result;
}

TFErrorCode DecodeResponse(const std::string& response, std::vector<uint8_t>& fps,
                           std::string& experiment_id) {
    using namespace json11;
    ALOGI("Response: %s", response.c_str());
    std::string err;
    Json jresponse = Json::parse(response, err);
    if (!err.empty()) {
        ALOGE("Parsing error: %s", err.c_str());
        return TFERROR_NO_FIDELITY_PARAMS;
    }
    ALOGI("Response, deserialized: %s", jresponse.dump().c_str());
    if (!jresponse.is_object()) {
        ALOGE("Response not object");
        return TFERROR_NO_FIDELITY_PARAMS;
    }
    auto& outer = jresponse.object_items();
    auto iparameters = outer.find("parameters");
    if (iparameters==outer.end()) {
        ALOGE("No parameters");
        return TFERROR_NO_FIDELITY_PARAMS;
    }
    auto& params = iparameters->second;
    if (!params.is_object()) {
        ALOGE("parameters not object");
        return TFERROR_NO_FIDELITY_PARAMS;
    }
    auto& inner = params.object_items();
    auto iexperiment_id = inner.find("experimentId");
    if (iexperiment_id==inner.end()) {
        ALOGE("No experimentId");
        return TFERROR_NO_FIDELITY_PARAMS;
    }
    if (!iexperiment_id->second.is_string()) {
        ALOGE("experimentId is not a string");
        return TFERROR_NO_FIDELITY_PARAMS;
    }
    experiment_id = iexperiment_id->second.string_value();
    auto ifps = inner.find("serializedFidelityParameters");
    if (ifps==inner.end()) {
        ALOGE("No serializedFidelityParameters");
        return TFERROR_NO_FIDELITY_PARAMS;
    }
    if (!ifps->second.is_string()) {
        ALOGE("serializedFidelityParameters is not a string");
        return TFERROR_NO_FIDELITY_PARAMS;
    }
    std::string sfps = ifps->second.string_value();
    fps.resize(modp_b64_decode_len(sfps.length()));
    if (modp_b64_decode((char*)fps.data(), sfps.c_str(), sfps.length())==-1) {
        ALOGE("Can't decode base 64 FPs");
        return TFERROR_NO_FIDELITY_PARAMS;
    }
    return TFERROR_OK;
}

#define CHECK_FOR_EXCEPTION if (jni.CheckForException(exception_msg)) { \
      ALOGW("%s", exception_msg.c_str()); return TFERROR_JNI_EXCEPTION; }

TFErrorCode DownloadFidelityParams(JNIEnv* env, jobject context, const std::string& uri,
                                   const std::string& api_key, const ExtraUploadInfo& requestInfo,
                                   int timeout_ms, std::vector<uint8_t>& fps,
                                   std::string& experiment_id) {
    ALOGI("Connecting to: %s", uri.c_str());
    JNIHelper jni(env);
    std::string exception_msg;
    // url = new URL(uri)
    jstring jurlStr = jni.NewString(uri);
    auto url = jni.NewObject("java/net/URL", "(Ljava/lang/String;)V", jurlStr);
    CHECK_FOR_EXCEPTION; // Malformed URL

    // Open connection and set properties
    // connection = url.openConnection()
    jobject connectionObj = jni.CallObjectMethod(url, "openConnection",
                                                 "()Ljava/net/URLConnection;");
    CHECK_FOR_EXCEPTION;// IOException
    auto connection = jni.Cast(connectionObj, "java/net/HttpURLConnection");
    // connection.setRequestMethod("POST")
    jni.CallVoidMethod(connection, "setRequestMethod", "(Ljava/lang/String;)V",
                       jni.NewString("POST"));
    // connection.setConnectionTimeout(timeout)
    jni.CallVoidMethod(connection, "setConnectTimeout", "(I)V", timeout_ms);
    // connection.setReadTimeout(timeout)
    jni.CallVoidMethod(connection, "setReadTimeout", "(I)V", timeout_ms);
    // connection.setDoOutput(true)
    jni.CallVoidMethod(connection, "setDoOutput", "(Z)V", true);
    // connection.setDoInput(true)
    jni.CallVoidMethod(connection, "setDoInput", "(Z)V", true);
    // connection.setUseCaches(false)
    jni.CallVoidMethod(connection, "setUseCaches", "(Z)V", false);
    // connection.setRequestProperty( name, value)
    if (!api_key.empty()) {
        jni.CallVoidMethod(connection, "setRequestProperty",
                           "(Ljava/lang/String;Ljava/lang/String;)V",
                           jni.NewString("X-Goog-Api-Key"), jni.NewString(api_key));
    }
    jni.CallVoidMethod(connection, "setRequestProperty", "(Ljava/lang/String;Ljava/lang/String;)V",
                       jni.NewString("Content-Type"), jni.NewString("application/json"));

    // Write json request body
    // os = connection.getOutputStream()
    jobject os = jni.CallObjectMethod(connection, "getOutputStream", "()Ljava/io/OutputStream;");
    CHECK_FOR_EXCEPTION; // IOException
    // writer = new BufferedWriter(new OutputStreamWriter(os, "UTF-8"))
    auto osw = jni.NewObject("java/io/OutputStreamWriter",
                             "(Ljava/io/OutputStream;Ljava/lang/String;)V",
                             os, jni.NewString("UTF-8"));
    auto writer = jni.NewObject("java/io/BufferedWriter", "(Ljava/io/Writer;)V", osw.second);
    // writer.write(json)
    jni.CallVoidMethod(writer, "write", "(Ljava/lang/String;)V",
                       jni.NewString(RequestJson(requestInfo)));
    CHECK_FOR_EXCEPTION;// IOException
    // writer.flush()
    jni.CallVoidMethod(writer, "flush", "()V");
    CHECK_FOR_EXCEPTION;// IOException
    // writer.close()
    jni.CallVoidMethod(writer, "close", "()V");
    CHECK_FOR_EXCEPTION;// IOException
    // os.close()
    jni.CallVoidMethod(jni.Cast(os), "close", "()V");
    CHECK_FOR_EXCEPTION;// IOException

    // connection.connect()
    jni.CallVoidMethod(connection, "connect", "()V");
    CHECK_FOR_EXCEPTION;// IOException

    // connection.getResponseCode()
    int code = jni.CallIntMethod(connection, "getResponseCode", "()I");
    ALOGI("Response code: %d", code);
    CHECK_FOR_EXCEPTION;// IOException

    // connection.getResponseMessage()
    jstring jresp = (jstring)jni.CallObjectMethod(connection, "getResponseMessage",
                                                  "()Ljava/lang/String;");
    const char* resp = env->GetStringUTFChars(jresp, nullptr);
    ALOGI("Response message: %s", resp);
    CHECK_FOR_EXCEPTION;// IOException

    // Read body from input stream
    jobject is = jni.CallObjectMethod(connection, "getInputStream", "()Ljava/io/InputStream;");
    CHECK_FOR_EXCEPTION;// IOException
    auto isr = jni.NewObject("java/io/InputStreamReader",
                             "(Ljava/io/InputStream;Ljava/lang/String;)V",
                             is, jni.NewString("UTF-8"));
    auto reader = jni.NewObject("java/io/BufferedReader", "(Ljava/io/Reader;)V", isr.second);
    std::stringstream body;
    jstring jline;
    while ((jline=(jstring)jni.CallObjectMethod(reader, "readLine",
                                                "()Ljava/lang/String;"))!=nullptr) {
        const char* line = env->GetStringUTFChars(jline, nullptr);
        body << line << "\n";
        env->ReleaseStringUTFChars(jline, line);
    }

    // reader.close()
    jni.CallVoidMethod(reader, "close", "()V");
    // is.close()
    jni.CallVoidMethod(jni.Cast(is), "close", "()V");

    // connection.disconnect()
    jni.CallVoidMethod(connection, "disconnect", "()V");

    TFErrorCode ret;
    if (code==200)
        ret = DecodeResponse(body.str(), fps, experiment_id);
    else
        ret = TFERROR_NO_FIDELITY_PARAMS;

    env->ReleaseStringUTFChars(jresp, resp);

    return ret;
}

TFErrorCode ParamsLoader::GetFidelityParams(JNIEnv* env, jobject context,
                                            const ExtraUploadInfo& info,
                                            const std::string& base_url,
                                            const std::string& api_key,
                                            ProtobufSerialization &fidelity_params,
                                            std::string& experiment_id,
                                            uint32_t timeout_ms) {
    std::stringstream url;
    url << base_url;
    url << GetPartialURL(info);
    url << url_rpcname;
    return DownloadFidelityParams(env, context, url.str(), api_key, info, timeout_ms,
                                  fidelity_params, experiment_id);
}

} // namespace tuningfork
