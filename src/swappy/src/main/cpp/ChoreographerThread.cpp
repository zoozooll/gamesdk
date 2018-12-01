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

#define LOG_TAG "ChoreographerThread"

#include <jni.h>

#include "ChoreographerThread.h"
#include "Log.h"
#include "Thread.h"
#include "Trace.h"

void ChoreographerThread::postFrameCallbacks()
{
    // This method is called before calling to swap buffers
    // It register to get maxCallbacksBeforeIdle frame callbacks before going idle
    // so if app goes to idle the thread will not get further frame callbacks
    std::lock_guard lock(mWaitingMutex);
    if (callbacksBeforeIdle == 0) {
        scheduleNextFrameCallback();
    }
    callbacksBeforeIdle = MAX_CALLBACKS_BEFORE_IDLE;
}

void ChoreographerThread::onChoreographer()
{
    {
        std::lock_guard lock(mWaitingMutex);
        callbacksBeforeIdle--;

        if (callbacksBeforeIdle > 0) {
            scheduleNextFrameCallback();
        }
    }
    mCallback();
}

#if __ANDROID_API__ >= 24
ChoreographerThread::ChoreographerThread(JavaVM *vm,
                                         std::function<void()> onChoreographer) :
   mCallback(onChoreographer),
   mThread([this]() {looperThread();})
{
    std::unique_lock<std::mutex> lock(mWaitingMutex);
    // create a new ALooper thread to get Choreographer events
    mThreadRunning = true;
    mWaitingCondition.wait(lock, [&]() REQUIRES(mWaitingMutex) {
        return mChoreographer != nullptr;
    });
}

ChoreographerThread::~ChoreographerThread()
{
    if (!mLooper) {
        return;
    }

    ALooper_acquire(mLooper);
    mThreadRunning = false;
    ALooper_wake(mLooper);
    ALooper_release(mLooper);
    mThread.join();
}

void ChoreographerThread::looperThread()
{
    int outFd, outEvents;
    void *outData;
    std::lock_guard lock(mWaitingMutex);

    mLooper = ALooper_prepare(0);
    if (!mLooper) {
        ALOGE("ALooper_prepare failed");
        return;
    }

    mChoreographer = AChoreographer_getInstance();
    if (!mChoreographer) {
        ALOGE("AChoreographer_getInstance failed");
        return;
    }
    mWaitingCondition.notify_all();

    while (mThreadRunning) {
        // mutex should be unlock before sleeping on pollAll
        mWaitingMutex.unlock();
        ALooper_pollAll(-1, &outFd, &outEvents, &outData);
        mWaitingMutex.lock();
    }

    return;
}

void ChoreographerThread::scheduleNextFrameCallback()
{
    AChoreographer_frameCallback frameCallback =
            [](long frameTimeNanos, void *data) {
                reinterpret_cast<ChoreographerThread*>(data)->onChoreographer();
            };

    AChoreographer_postFrameCallbackDelayed(mChoreographer, frameCallback, this, 1);
}

#else // __ANDROID_API__ >= 24
ChoreographerThread::ChoreographerThread(JavaVM *vm,
                                         std::function<void()> onChoreographer) :
        mJVM(vm),
        mCallback(onChoreographer) {

    JNIEnv *env;
    mJVM->AttachCurrentThread(&env, nullptr);

    jclass choreographerCallbackClass = env->FindClass("com/google/swappy/ChoreographerCallback");

    jmethodID constructor = env->GetMethodID(
            choreographerCallbackClass,
            "<init>",
            "(J)V");

    mChorMan_postFrameCallback = env->GetMethodID(
            choreographerCallbackClass,
            "postFrameCallback",
            "()V");

    mChorMan_Terminate = env->GetMethodID(
            choreographerCallbackClass,
            "Terminate",
            "()V");

    jobject choreographerCallback = env->NewObject(choreographerCallbackClass, constructor, reinterpret_cast<jlong>(this));

    mChorMan = env->NewGlobalRef(choreographerCallback);
}

ChoreographerThread::~ChoreographerThread()
{
    if (!mChorMan) {
        return;
    }

    JNIEnv *env;
    mJVM->AttachCurrentThread(&env, nullptr);
    env->CallVoidMethod(mChorMan, mChorMan_Terminate);
    env->DeleteGlobalRef(mChorMan);
    mJVM->DetachCurrentThread();
}

void ChoreographerThread::scheduleNextFrameCallback()
{
    JNIEnv *env;
    mJVM->AttachCurrentThread(&env, nullptr);
    env->CallVoidMethod(mChorMan, mChorMan_postFrameCallback);
}

// JNI interface
class ChoreographerCallback {
public:
    static void onChoreographer(jlong cookie) {
        reinterpret_cast<ChoreographerThread*>(cookie)->onChoreographer();
    }
};

extern "C" {

JNIEXPORT void JNICALL
Java_com_google_swappy_ChoreographerCallback_nOnChoreographer(JNIEnv * /* env */, jobject /* this */,
                                                                jlong cookie, jlong frameTimeNanos) {
    ChoreographerCallback::onChoreographer(cookie);
}

} // extern "C"

#endif // __ANDROID_API__ >= 24