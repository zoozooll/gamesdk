package com.google.tuningfork;

import android.os.Bundle;
import android.support.v7.app.AppCompatActivity;
import android.util.Log;
import android.view.Choreographer;

public class TFTestActivity extends AppCompatActivity implements Choreographer.FrameCallback {


    // Used to load the 'native-lib' library on application startup.
    static {
        System.loadLibrary("native-lib");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        nInit();
    }

    @Override
    protected void onStart() {
        super.onStart();
        Choreographer.getInstance().postFrameCallback(this);
    }

    @Override
    public void doFrame(long t) {

        nOnChoreographer(t);
        Choreographer.getInstance().postFrameCallback(this);
    }

    public native void nInit();
    public native void nOnChoreographer(long frameTimeNanos);

}
