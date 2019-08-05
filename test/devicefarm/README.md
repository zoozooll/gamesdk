# devicefarm

This script allows to easily run a test on many
[Firebase Test Lab](https://firebase.google.com/docs/test-lab) devices at once.
It also automatically downloads logs for the tests after they're finished.

### FTL

[Firebase Test Lab](https://firebase.google.com/docs/test-lab) provides cloud
testing on real physical devices.

## Requirements

The script requires the [Cloud SDK](http://cloud/sdk/install) to be installed.

## Running

The script takes the same arguments as the `gcloud` CLI utility. The
`flags.yaml` file is always used, but additional (or overriding) flags can be
specified on the command line.

A sample flags args file is provided. For example, running
```
./run.py args.yaml:bouncyball-robo
```
will by default run a 1 minute robo test on 3 devices using the "Hello World"
sample app. Make sure to build it first:
```
cd hello
./gradlew assemble
```

The "Hello World" sample app prints the phrase "Hello World!" to the logs as an
easy way to verify eventual post-processing pipelines.

`args.yaml` can contain different argument groups (the part after the colon)
which will allow different tests to be run. For more information on arg files,
run `gcloud topic arg-files`.

The file `flags.yaml` contains flags which, among other things, set the active
current project. **Edit this before first use to ensure the correct project is
used.** For more information on flags files, run `gcloud topic flags-file`.

### Which models/API levels can I specify?

The command
```
gcloud firebase test android models list
```
returns a table with the available devices and API levels.

## Results

The logs are downloaded to a new folder. Each file is named in the scheme
```
<Model>-<API Level>-<Language>-<Orientation>_logcat
```
Default language is `en` and default orientation is `portrait`.

