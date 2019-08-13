# Running Tests on the FTL device farm

This directory contains tests which are designed to be run on the
[Firebase Test Lab](https://firebase.google.com/docs/test-lab) farm.

It contains the `run.py` script which will invoke a specified test on the farm as
well as automatically downloading logs for the tests after they're finished.

### FTL

[Firebase Test Lab](https://firebase.google.com/docs/test-lab) provides cloud
testing on real physical devices.

## Setup

The script requires the [Cloud SDK](http://cloud/sdk/) to be installed.

Follow [these instructions](http://cloud/sdk/install)  to install the SDK.

If you are working on a Google corp workstation you should follow these setup
instructions instead [go/cloudsdk](http://go/cloudsdk).

Once the SDK is installed login to gCloud with:

```
gcloud auth login
```

## Running A Test

To verify that everything is set up correctly, run the Hello World test on the
device farm.

First navigate to the gamesdk/test/devicefarm/ directory which contains the
script `run.py`.

The source of the Hello World test is located in the gamesdk/test/devicefarm/hello/
directory, first build the code:

```
cd hello
./gradlew assemble
cd ..
```

Then use run.py to run it on the device farm. The `flags.yaml` file is provided and
contains useful defaults. This file should always used but additional (or overriding)
flags can be specified on the command line. Run:

```
./run.py args.yaml:test-robo
```

To run Hello World on the 3 default devices.
Hello World app prints the phrase "Hello World!" to the logs as an
easy way to verify eventual post-processing pipelines.

This should create a directory with a time stamp based name. That directory will
contain several log files created by the test (one for each device). If you
navigate to that directory and run:

```
grep Hello *
```

You should see output similar to:

    blueline-28-en-portrait_logcat:08-13 03:47:41.753: D/MainActivity(16266): Hello World!
    taimen-27-en-portrait_logcat:08-13 03:47:30.502: D/MainActivity(25598): Hello World!
    walleye-28-en-portrait_logcat:08-13 03:47:50.351: D/MainActivity(13378): Hello World!

Each file is named in the scheme:

```
<Model>-<API Level>-<Language>-<Orientation>_logcat
```

Default language is `en` and default orientation is `portrait`.

### More info about args.yaml

`args.yaml` can contain different argument groups (the part after the colon)
which will allow different tests to be run. For more information on arg files,
run `gcloud topic arg-files`.

The file `flags.yaml` contains flags which, among other things, set the active
current project. For more information on flags files, run `gcloud topic flags-file`.

### Which models/API levels can I specify?

The command

```
gcloud firebase test android models list
```

returns a table with the available devices and API levels.

### Troubleshooting

If you recieve an error along the lines of:

```
ERROR: (gcloud.firebase.test.android.run) Unable to access the test environment catalog: ResponseError 403: Not authorized for project gamesdk-testing
```

This indicates that you do not have access to the FTL project that is specified
in `flags.yaml`. Please contact your project administrator to rectify this.

