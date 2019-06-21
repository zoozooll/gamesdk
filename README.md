# Android Game SDK

## Build the Game SDK

In order to build using prebuild NDK versions, this project must be initialized from a custom repo using:

```bash
mkdir android-games-sdk
cd android-games-sdk
repo init -u https://android.googlesource.com/platform/manifest -b android-games-sdk
# Or for Googlers:
# repo init -u persistent-https://googleplex-android.git.corp.google.com/platform/manifest -b android-games-sdk
repo sync -c -j8
```

### Build with prebuilt SDKs

```bash
cd gamesdk
ANDROID_HOME=../prebuilts/sdk ./gradlew gamesdkZip
```

will build static and dynamic libraries for several SDK/NDK pairs.

### Build with locally installed SDK/NDK

By default, the gradle script builds target `archiveZip`.

```bash
./gradlew archiveZip # Without Tuning Fork
./gradlew archiveTfZip # With Tuning Fork
```

This will use a locally installed SDK/NDK pointed to by `ANDROID_HOME` (and `ANDROID_NDK`, if the ndk isn't in `ANDROID_HOME/ndk-bundle`).

## Samples

Samples are classic Android projects, using CMake to build the native code. They are also all triggering the build of the Game SDK.

### Using Grade command line:

```bash
cd samples/bouncyball && ./gradlew assemble
cd samples/cube && ./gradlew assemble
cd samples/tuningfork/tftestapp && ./gradlew assemble
```

The Android SDK/NDK exposed using environment variables (`ANDROID_HOME`) will be used for building both the sample project and the Game SDK.

### Using Android Studio

Open projects using Android Studio:

* `samples/bouncyball`
* `samples/cube`
* `samples/tuningfork/tftestapp`

and run them directly (`Shift + F10` on Linux, `Control + R` on macOS). The local Android SDK/NDK (configured in Android Studio) will be used for building both the sample project and the Game SDK.