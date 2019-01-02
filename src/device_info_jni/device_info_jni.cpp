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

#include <jni.h>

#include "device_info/device_info.h"

extern "C" {
JNIEXPORT jbyteArray JNICALL
Java_com_google_androidgamesdk_DeviceInfoJni_getProtoSerialized(
                                        JNIEnv *env, jobject) {
  androidgamesdk_deviceinfo::Root proto;
  androidgamesdk_deviceinfo::createProto(proto);

  size_t bufferSize = proto.ByteSize();
  void* buffer = malloc(bufferSize);
  proto.SerializeToArray(buffer, bufferSize);
  jbyteArray result = env->NewByteArray(bufferSize);
  env->SetByteArrayRegion(result, 0, bufferSize, static_cast<jbyte*>(buffer));
  free(buffer);
  return result;
}
}  // extern "C"
