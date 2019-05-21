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