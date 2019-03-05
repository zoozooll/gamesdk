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

#include <sstream>
#include "uploadthread.h"
#include "clearcutserializer.h"
#include "modp_b64.h"

#define LOG_TAG "TuningFork"
#include "Log.h"

namespace tuningfork {

DebugBackend::~DebugBackend() {}

bool DebugBackend::Process(const ProtobufSerialization &evt_ser) {
    if (evt_ser.size() == 0) return false;
    auto encode_len = modp_b64_encode_len(evt_ser.size());
    std::vector<char> dest_buf(encode_len);
    // This fills the dest buffer with a null-terminated string. It returns the length of
    //  the string, not including the null char
    auto n_encoded = modp_b64_encode(&dest_buf[0], reinterpret_cast<const char*>(&evt_ser[0]),
        evt_ser.size());
    if (n_encoded == -1 || encode_len != n_encoded+1) {
        ALOGW("Could not b64 encode protobuf");
        return false;
    }
    std::string s(&dest_buf[0], n_encoded);
    // Split the serialization into <128-byte chunks to avoid logcat line
    //  truncation.
    constexpr size_t maxStrLen = 128;
    int n = (s.size() + maxStrLen - 1) / maxStrLen; // Round up
    for (int i = 0, j = 0; i < n; ++i) {
        std::stringstream str;
        str << "(TCL" << (i + 1) << "/" << n << ")";
        int m = std::min(s.size() - j, maxStrLen);
        str << s.substr(j, m);
        j += m;
        ALOGI("%s", str.str().c_str());
    }
    return true;
}

std::unique_ptr<DebugBackend> s_debug_backend = std::make_unique<DebugBackend>();

UploadThread::UploadThread(Backend *backend) : backend_(backend),
                                               current_fidelity_params_(0) {
    if (backend_ == nullptr)
        backend_ = s_debug_backend.get();
    Start();
}

UploadThread::~UploadThread() {
    Stop();
}

void UploadThread::Start() {
    if (thread_) {
        ALOGW("Can't start an already running thread");
        return;
    }
    do_quit_ = false;
    ready_ = nullptr;
    thread_ = std::make_unique<std::thread>([&] { return Run(); });
}

void UploadThread::Stop() {
    if (!thread_->joinable()) {
        ALOGW("Can't stop a thread that's not started");
        return;
    }
    do_quit_ = true;
    cv_.notify_one();
    thread_->join();
}

void UploadThread::Run() {
    while (!do_quit_) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (ready_) {
            ProtobufSerialization evt_ser;
            ClearcutSerializer::SerializeEvent(*ready_, current_fidelity_params_, evt_ser);
            backend_->Process(evt_ser);
            ready_ = nullptr;
        }
        cv_.wait_for(lock, std::chrono::milliseconds(1000));
    }
}

// Returns true if we submitted, false if we are waiting for a previous submit to complete
bool UploadThread::Submit(const ProngCache *prongs) {
    if (ready_ == nullptr) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ready_ = prongs;
        }
        cv_.notify_one();
        return true;
    } else
        return false;
}

} // namespace tuningfork
