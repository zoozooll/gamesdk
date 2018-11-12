#include <jni.h>
#include "histogram_test.h"
#include "tuningfork_test.h"

namespace {
  jstring javaString(JNIEnv * env, const std::string& s) {
    return env->NewStringUTF(s.c_str());
  }
}
extern "C" {

JNIEXPORT jstring JNICALL
Java_com_google_tuningfork_Tests_defaultEmpty(JNIEnv * env , jobject /* this */) {
  auto result = histogram_test::TestDefaultEmpty();
  return javaString(env, result);
}

JNIEXPORT jstring JNICALL
Java_com_google_tuningfork_Tests_empty0To10(JNIEnv * env , jobject /* this */) {
  auto result = histogram_test::TestEmpty0To10();
  return javaString(env, result);
}

JNIEXPORT jstring JNICALL
Java_com_google_tuningfork_Tests_addOneToAutoSizing(JNIEnv * env , jobject /* this */) {
  auto result = histogram_test::TestAddOneToAutoSizing();
  return javaString(env, result);
}

JNIEXPORT jstring JNICALL
Java_com_google_tuningfork_Tests_addOneTo0To10(JNIEnv * env , jobject /* this */) {
  auto result = histogram_test::TestAddOneTo0To10();
  return javaString(env, result);
}

JNIEXPORT jstring JNICALL
Java_com_google_tuningfork_Tests_endToEnd(JNIEnv * env , jobject /* this */) {
  auto result = tuningfork_test::TestEndToEnd();
  return javaString(env, result);
}

JNIEXPORT jstring JNICALL
Java_com_google_tuningfork_Tests_endToEndWithAnnotation(JNIEnv * env , jobject /* this */) {
  auto result = tuningfork_test::TestEndToEndWithAnnotation();
  return javaString(env, result);
}

JNIEXPORT jstring JNICALL
Java_com_google_tuningfork_Tests_endToEndTimeBased(JNIEnv * env , jobject /* this */) {
  auto result = tuningfork_test::TestEndToEndTimeBased();
  return javaString(env, result);
}

JNIEXPORT jstring JNICALL
Java_com_google_tuningfork_Tests_endToEndWithStaticHistogram(JNIEnv * env , jobject /* this */) {
  auto result = tuningfork_test::TestEndToEndWithStaticHistogram();
  return javaString(env, result);
}

JNIEXPORT jboolean JNICALL
    Java_com_google_tuningfork_Tests_usingProtobufLite(JNIEnv * env , jobject /* this */) {
#ifdef PROTOBUF_LITE
  return true;
#else
  return false;
#endif
}

}
