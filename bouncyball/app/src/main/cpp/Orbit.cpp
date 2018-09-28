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

#define LOG_TAG "Orbit"

#include <string>

#include <jni.h>

#include <android/native_window_jni.h>

#include "swappy-utils/Log.h"
#include "swappy-utils/Settings.h"

#include "swappy/Swappy.h"

#include "Renderer.h"

using std::chrono::nanoseconds;

namespace {
std::string to_string(jstring jstr, JNIEnv *env) {
    const char *utf = env->GetStringUTFChars(jstr, nullptr);
    std::string str(utf);
    env->ReleaseStringUTFChars(jstr, utf);
    return str;
}
} // anonymous namespace

extern "C" {

JNIEXPORT void JNICALL
Java_com_prefabulated_bouncyball_OrbitActivity_nInit(JNIEnv * /* env */, jobject /* this */,
                                              jlong vsyncPeriodNanos, jlong appVsyncOffsetNanos,
                                              jlong sfVsyncOffsetNanos) {
    // Get the Renderer instance to create it
    Renderer::getInstance();

    Swappy::init(nanoseconds(vsyncPeriodNanos), nanoseconds(appVsyncOffsetNanos),
                 nanoseconds(sfVsyncOffsetNanos));
}

JNIEXPORT void JNICALL
Java_com_prefabulated_bouncyball_OrbitActivity_nSetSurface(JNIEnv *env, jobject /* this */,
                                                    jobject surface, jint width, jint height) {
    ANativeWindow *window = ANativeWindow_fromSurface(env, surface);
    Renderer::getInstance()->setWindow(window,
                                     static_cast<int32_t>(width),
                                     static_cast<int32_t>(height));
}

JNIEXPORT void JNICALL
Java_com_prefabulated_bouncyball_OrbitActivity_nClearSurface(JNIEnv * /* env */, jobject /* this */) {
    Renderer::getInstance()->setWindow(nullptr, 0, 0);
}

JNIEXPORT void JNICALL
Java_com_prefabulated_bouncyball_OrbitActivity_nStart(JNIEnv * /* env */, jobject /* this */) {
    ALOGI("start");
    Renderer::getInstance()->start();
}

JNIEXPORT void JNICALL
Java_com_prefabulated_bouncyball_OrbitActivity_nStop(JNIEnv * /* env */, jobject /* this */) {
    ALOGI("stop");
    Renderer::getInstance()->stop();
}

JNIEXPORT void JNICALL
Java_com_prefabulated_bouncyball_OrbitActivity_nOnChoreographer(JNIEnv * /* env */, jobject /* this */,
                                                         jlong frameTimeNanos) {
    Swappy::onChoreographer(frameTimeNanos);
}

JNIEXPORT void JNICALL
Java_com_prefabulated_bouncyball_OrbitActivity_nSetPreference(JNIEnv *env, jobject /* this */,
                                                         jstring key, jstring value) {
    Settings::getInstance()->setPreference(to_string(key, env), to_string(value, env));
}

} // extern "C"