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

#pragma once

#include <jni.h>
#include <vector>

namespace tuningfork {

// A helper class that makes calling methods easier and also keeps track of object/string references
//  and deletes them when the helper is destroyed.
class JNIHelper {
    JNIEnv* env_;
    std::vector<jobject> objs_;
    jmethodID find_class_;
    jobject activity_class_loader_;
  public:
    typedef std::pair<jclass,jobject> Object;
    JNIHelper(JNIEnv* env, jobject activity) : env_(env) {
        jclass activity_clazz = env->GetObjectClass(activity);
        jmethodID get_class_loader = env->GetMethodID(
            activity_clazz, "getClassLoader", "()Ljava/lang/ClassLoader;");
        activity_class_loader_ =
            env->CallObjectMethod(activity, get_class_loader);

        jclass class_loader = env->FindClass("java/lang/ClassLoader");

        find_class_ = env->GetMethodID(
            class_loader, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    }
    ~JNIHelper() {
        for(auto& o: objs_)
            env_->DeleteLocalRef(o);
    }

    jclass FindClass(const char* class_name) {
        jclass jni_class = env_->FindClass(class_name);

        if (jni_class == NULL) {
            // FindClass would have thrown.
            env_->ExceptionClear();
            jstring class_jname = env_->NewStringUTF(class_name);
            jni_class =
                (jclass)(env_->CallObjectMethod(activity_class_loader_, find_class_, class_jname));
            env_->DeleteLocalRef(class_jname);
        }
        return jni_class;
    }

    Object NewObject(const char * cclz, const char* ctorSig, ...) {
        jclass clz = FindClass(cclz);
        jmethodID constructor = env_->GetMethodID(clz, "<init>", ctorSig);
        va_list argptr;
        va_start(argptr, ctorSig);
        jobject o = env_->NewObjectV(clz, constructor, argptr);
        va_end(argptr);
        objs_.push_back(o);
        return {clz, o};
    }
    jobject CallObjectMethod(const Object& obj, const char* name, const char* sig, ...) {
        jmethodID mid = env_->GetMethodID(obj.first, name, sig);
        va_list argptr;
        va_start(argptr, sig);
        jobject o = env_->CallObjectMethodV(obj.second, mid, argptr);
        va_end(argptr);
        objs_.push_back(o);
        return o;
    }
    Object Cast(jobject o, const std::string& clz="") {
        if(clz.empty())
            return {env_->GetObjectClass(o), o};
        else
            return {FindClass(clz.c_str()), o};
    }
    void CallVoidMethod(const Object& obj, const char* name, const char* sig, ...) {
        jmethodID mid = env_->GetMethodID(obj.first, name, sig);
        va_list argptr;
        va_start(argptr, sig);
        env_->CallVoidMethodV(obj.second, mid, argptr);
        va_end(argptr);
    }
    int CallIntMethod(const Object& obj, const char* name, const char* sig, ...) {
        jmethodID mid = env_->GetMethodID(obj.first, name, sig);
        va_list argptr;
        va_start(argptr, sig);
        int r = env_->CallIntMethodV(obj.second, mid, argptr);
        va_end(argptr);
        return r;
    }
    jstring NewString(const char* c) {
        auto s = env_->NewStringUTF(c);
        objs_.push_back(s);
        return s;
    }
    jstring NewString(const std::string& s) {
        auto js = env_->NewStringUTF(s.c_str());
        objs_.push_back(js);
        return js;
    }
    bool CheckForException(std::string& msg) {
        if(env_->ExceptionCheck()) {
            jthrowable exception = env_->ExceptionOccurred();
            env_->ExceptionClear();
            jmethodID toString = env_->GetMethodID(FindClass("java/lang/Object"),
                "toString", "()Ljava/lang/String;");
            jstring s = (jstring)env_->CallObjectMethod(exception, toString);
            const char* utf = env_->GetStringUTFChars(s, nullptr);
            msg = utf;
            env_->ReleaseStringUTFChars(s, utf);
            env_->DeleteLocalRef(s);
            return true;
        }
        return false;
    }
};

} // namespace tuningfork
