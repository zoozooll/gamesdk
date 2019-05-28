/*
 * Copyright 2019 The Android Open Source Project
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

// Common part between swappyGL.h and swappyVk.h

#pragma once

// swap interval constant helpers
#define SWAPPY_SWAP_60FPS (16666667L)
#define SWAPPY_SWAP_30FPS (33333333L)
#define SWAPPY_SWAP_20FPS (50000000L)

#define SWAPPY_SYSTEM_PROP_KEY_DISABLE "swappy.disable"

// Internal macros to track Swappy version, do not use directly.
#define SWAPPY_MAJOR_VERSION 0
#define SWAPPY_MINOR_VERSION 1
#define SWAPPY_PACKED_VERSION ((SWAPPY_MAJOR_VERSION<<16)|(SWAPPY_MINOR_VERSION))

// Internal macros to generate a symbol to track Swappy version, do not use directly.
#define SWAPPY_VERSION_CONCAT_NX(PREFIX, MAJOR, MINOR) PREFIX ## _ ## MAJOR ## _ ## MINOR
#define SWAPPY_VERSION_CONCAT(PREFIX, MAJOR, MINOR) SWAPPY_VERSION_CONCAT_NX(PREFIX, MAJOR, MINOR)
#define SWAPPY_VERSION_SYMBOL SWAPPY_VERSION_CONCAT(Swappy_version, SWAPPY_MAJOR_VERSION, SWAPPY_MINOR_VERSION)

#ifdef __cplusplus
extern "C" {
#endif

// Internal function to track Swappy version bundled in a binary. Do not call directly.
// If you are getting linker errors related to Swappy_version_x_y, you probably have a
// mismatch between the header used at compilation and the actually library used by the linker.
void SWAPPY_VERSION_SYMBOL();

#ifdef __cplusplus
}  // extern "C"
#endif