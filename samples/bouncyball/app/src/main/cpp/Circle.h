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

#include <array>

#include <GLES2/gl2.h>

struct Circle {
    struct Color {
        Color(GLfloat r, GLfloat g, GLfloat b) : r(r), g(g), b(b) {}

        union {
            std::array<GLfloat, 3> values;
            struct {
                GLfloat r;
                GLfloat g;
                GLfloat b;
            };
        };
    };

    Circle(const Color &color, float radius, float x, float y) : color(color), radius(radius), x(x),
                                                                 y(y) {};

    static void draw(float aspectRatio, const std::vector<Circle> &circles);

    static const size_t NUM_SEGMENTS = 36;
    static const size_t NUM_VERTICES = 2 * (NUM_SEGMENTS + 2);

    static std::array<GLfloat, NUM_VERTICES> &getVertices();

    const Color color;
    const float radius;
    const float x;
    const float y;
};
