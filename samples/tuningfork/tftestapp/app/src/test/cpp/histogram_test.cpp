/*
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

#include "../../main/cpp/histogram.h"

#include "histogram_test.h"

using namespace tuningfork;

namespace histogram_test {

std::string TestDefaultEmpty() {
    Histogram h;
    return h.ToJSON();
}

std::string TestEmpty0To10() {
    Histogram h({0, 10, 10});
    return h.ToJSON();
}

std::string TestAddOneToAutoSizing() {
    Histogram h({0, 0, 8});
    h.Add(1.0);
    h.CalcBucketsFromSamples();
    return h.ToJSON();
}

std::string TestAddOneTo0To10() {
    Histogram h({0, 10, 10});
    h.Add(1.0);
    return h.ToJSON();
}

}

