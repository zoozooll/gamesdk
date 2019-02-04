package com.google.tuningfork;

import static org.junit.Assert.*;

import android.content.Context;

import androidx.test.InstrumentationRegistry;
import androidx.test.runner.AndroidJUnit4;

import org.junit.Test;
import org.junit.runner.RunWith;

/**
 * Instrumented test, which will execute on an Android device.
 *
 * @see <a href="http://d.android.com/tools/testing">Testing documentation</a>
 */
@RunWith(AndroidJUnit4.class)
public class Tests {
    @Test
    public void useAppContext() {
        // Context of the app under test.
        Context appContext = InstrumentationRegistry.getTargetContext();

        assertEquals("com.google.tuningfork", appContext.getPackageName());
    }

    // Used to load the 'native-lib' library on application startup.
    static {
        System.loadLibrary("native-lib");
    }

    @Test
    public void testHistogram() {
        assertEquals("{\"pmax\":[],\"cnts\":[]}", defaultEmpty());
        assertEquals("{\"pmax\":[0.00,1.00,2.00,3.00,4.00,5.00,6.00,7.00,8.00,9.00,10.00,99999],\"cnts\":[0,0,0,0,0,0,0,0,0,0,0,0]}", empty0To10());
        assertEquals("{\"pmax\":[0.60,0.70,0.80,0.90,1.00,1.10,1.20,1.30,1.40,99999],\"cnts\":[0,0,0,0,0,1,0,0,0,0]}", addOneToAutoSizing());
        assertEquals("{\"pmax\":[0.00,1.00,2.00,3.00,4.00,5.00,6.00,7.00,8.00,9.00,10.00,99999],\"cnts\":[0,0,1,0,0,0,0,0,0,0,0,0]}", addOneTo0To10());
        if(!usingProtobufLite()) {
            assertEquals("fidelityparams {\n}\n" +
                    "histograms {\n" +
                    "  instrument_id: 0\n" +
                    "  annotation {\n" +
                    "  }\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 100\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "}\n", endToEnd());
            assertEquals("fidelityparams {\n}\n" +
                    "histograms {\n" +
                    "  instrument_id: 1\n" +
                    "  annotation {\n" +
                    "    [level]: LEVEL_1\n" +
                    "  }\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 100\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "}\n", endToEndWithAnnotation());
            assertEquals("fidelityparams {\n}\n" +
                    "histograms {\n" +
                    "  instrument_id: 0\n" +
                    "  annotation {\n" +
                    "  }\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 100\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "}\n", endToEndTimeBased());
            assertEquals("fidelityparams {\n}\n" +
                    "histograms {\n" +
                    "  instrument_id: 0\n" +
                    "  annotation {\n" +
                    "  }\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 100\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "  counts: 0\n" +
                    "}\n", endToEndWithStaticHistogram());
        }
    }

    public native String defaultEmpty();
    public native String empty0To10();
    public native String addOneToAutoSizing();
    public native String addOneTo0To10();
    public native String endToEnd();
    public native String endToEndWithAnnotation();
    public native String endToEndTimeBased();
    public native String endToEndWithStaticHistogram();
    public native boolean usingProtobufLite();

}
