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

#include "EGL.h"

#define LOG_TAG "Swappy::EGL"

#include "Log.h"

using namespace std::chrono_literals;

namespace swappy {

std::unique_ptr<EGL> EGL::create(std::chrono::nanoseconds refreshPeriod) {
    auto eglPresentationTimeANDROID = reinterpret_cast<eglPresentationTimeANDROID_type>(
        eglGetProcAddress("eglPresentationTimeANDROID"));
    if (eglPresentationTimeANDROID == nullptr) {
        ALOGE("Failed to load eglPresentationTimeANDROID");
        return nullptr;
    }

    auto eglCreateSyncKHR = reinterpret_cast<eglCreateSyncKHR_type>(
        eglGetProcAddress("eglCreateSyncKHR"));
    if (eglCreateSyncKHR == nullptr) {
        ALOGE("Failed to load eglCreateSyncKHR");
        return nullptr;
    }

    auto eglDestroySyncKHR = reinterpret_cast<eglDestroySyncKHR_type>(
        eglGetProcAddress("eglDestroySyncKHR"));
    if (eglDestroySyncKHR == nullptr) {
        ALOGE("Failed to load eglDestroySyncKHR");
        return nullptr;
    }

    auto eglGetSyncAttribKHR = reinterpret_cast<eglGetSyncAttribKHR_type>(
        eglGetProcAddress("eglGetSyncAttribKHR"));
    if (eglGetSyncAttribKHR == nullptr) {
        ALOGE("Failed to load eglGetSyncAttribKHR");
        return nullptr;
    }

    auto egl = std::make_unique<EGL>(refreshPeriod, ConstructorTag{});
    egl->eglPresentationTimeANDROID = eglPresentationTimeANDROID;
    egl->eglCreateSyncKHR = eglCreateSyncKHR;
    egl->eglDestroySyncKHR = eglDestroySyncKHR;
    egl->eglGetSyncAttribKHR = eglGetSyncAttribKHR;
    return egl;
}

void EGL::resetSyncFence(EGLDisplay display) {
    std::lock_guard<std::mutex> lock(mSyncFenceMutex);

    if (mSyncFence != EGL_NO_SYNC_KHR) {
        EGLBoolean result = eglDestroySyncKHR(display, mSyncFence);
        if (result == EGL_FALSE) {
            ALOGE("Failed to destroy sync fence");
        }
    }

    mSyncFence = eglCreateSyncKHR(display, EGL_SYNC_FENCE_KHR, nullptr);
}

bool EGL::lastFrameIsComplete(EGLDisplay display) {
    std::lock_guard<std::mutex> lock(mSyncFenceMutex);

    // This will be the case on the first frame
    if (mSyncFence == EGL_NO_SYNC_KHR) {
        return true;
    }

    EGLint status = 0;
    EGLBoolean result = eglGetSyncAttribKHR(display, mSyncFence, EGL_SYNC_STATUS_KHR, &status);
    if (result == EGL_FALSE) {
        ALOGE("Failed to get sync status");
        return true;
    }

    if (status == EGL_SIGNALED_KHR) {
        return true;
    } else if (status == EGL_UNSIGNALED_KHR) {
        return false;
    } else {
        ALOGE("Unexpected sync status: %d", status);
        return true;
    }
}

bool EGL::setPresentationTime(EGLDisplay display,
                              EGLSurface surface,
                              std::chrono::steady_clock::time_point time) {
    eglPresentationTimeANDROID(display, surface, time.time_since_epoch().count());
    return EGL_TRUE;
}

} // namespace swappy
