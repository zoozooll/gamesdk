/*
 * Copyright (C) 2019 The Android Open Source Project
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
 * limitations under the License
 */

package com.google.tuningfork.validation;

/** Validation errors */
public enum ErrorType {
  ANNOTATION_EMPTY, // Annotation field is empty
  ANNOTATION_COMPLEX, // Annotation field is too complex - contains oneofs/nestedtypes/extensions
  ANNOTATION_TYPE, // Annotation must contains enums only

  FIDELITY_PARAMS_EMPTY, // FidelityParams fied is empty
  FIDELITY_PARAMS_COMPLEX, // FidelityParams field is complex - contains
                           // oneof/nestedtypes/extensions
  FIDELITY_PARAMS_TYPE, // FidelityParams can only contains float, int32 or enum

  DEV_FIDELITY_PARAMETERS_EMPTY, // Fidelity parameters are empty
  DEV_FIDELITY_PARAMETERS_PARSING, // Fidelity parameters parsing error

  SETTINGS_PARSING, // Parsing error

  HISTOGRAM_EMPTY, // Histogram field is empty

  AGGREGATION_EMPTY, // Aggreagtion field is empty
  AGGREGATION_INSTRUMENTATION_KEY, // Aggregation contains incorrect max_instrumentation_keys field
  AGGREGATION_ANNOTATIONS, // Aggregation contains incorrect annotation_enum_sizes
};
