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

#pragma once

#include <mutex>

#include <EGL/egl.h>
#include <EGL/eglext.h>

class EGL {
  private:
    // Allows construction with std::unique_ptr from a static method, but disallows construction
    // outside of the class since no one else can construct a ConstructorTag
    struct ConstructorTag {
    };

  public:
    explicit EGL(std::chrono::nanoseconds refreshPeriod, ConstructorTag)
        : mRefreshPeriod(refreshPeriod) {}

    static std::unique_ptr<EGL> create(std::chrono::nanoseconds refreshPeriod);

    void resetSyncFence(EGLDisplay display);
    bool lastFrameIsComplete(EGLDisplay display);
    bool setPresentationTime(EGLDisplay display, EGLSurface surface, int32_t swapInterval);

  private:
    const std::chrono::nanoseconds mRefreshPeriod;

    using eglPresentationTimeANDROID_type = EGLBoolean (*)(EGLDisplay, EGLSurface, EGLnsecsANDROID);
    eglPresentationTimeANDROID_type eglPresentationTimeANDROID = nullptr;
    using eglCreateSyncKHR_type = EGLSyncKHR (*)(EGLDisplay, EGLenum, const EGLint *);
    eglCreateSyncKHR_type eglCreateSyncKHR = nullptr;
    using eglDestroySyncKHR_type = EGLBoolean (*)(EGLDisplay, EGLSyncKHR);
    eglDestroySyncKHR_type eglDestroySyncKHR = nullptr;
    using eglGetSyncAttribKHR_type = EGLBoolean (*)(EGLDisplay, EGLSyncKHR, EGLint, EGLint *);
    eglGetSyncAttribKHR_type eglGetSyncAttribKHR = nullptr;

    std::mutex mSyncFenceMutex;
    EGLSyncKHR mSyncFence = EGL_NO_SYNC_KHR;
};
