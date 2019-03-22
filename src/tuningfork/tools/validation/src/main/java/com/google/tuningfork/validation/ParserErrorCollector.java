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

import com.google.common.collect.LinkedListMultimap;
import com.google.common.collect.ListMultimap;
import com.google.common.collect.Multimap;
import com.google.common.flogger.FluentLogger;

/** Collects validation errors */
final class ParserErrorCollector implements ErrorCollector {
  private static final FluentLogger logger = FluentLogger.forEnclosingClass();

  private final ListMultimap<ErrorType, String> errors = LinkedListMultimap.create();

  @Override
  public Multimap<ErrorType, String> getErrors() {
    return errors;
  }

  @Override
  public void addError(ErrorType errorType, String message) {
    errors.put(errorType, message);
  }

  @Override
  public void addError(ErrorType errorType, String message, Exception e) {
    errors.put(errorType, message);
  }

  @Override
  public Integer getErrorCount() {
    return errors.size();
  }

  @Override
  public Integer getErrorCount(ErrorType errorType) {
    return errors.get(errorType).size();
  }

  @Override
  public void printStatus() {
    StringBuilder builder = new StringBuilder();
    for (ErrorType errorType : ErrorType.values()) {
      builder.append(errorType).append(" : ");
      int errorCount = errors.get(errorType).size();
      if (errorCount == 0) {
        builder.append("OK");
      } else {
        builder.append(errorCount);
        builder.append(" ERRORS\n\t");
        builder.append(errors.get(errorType));
      }
      builder.append("\n");
    }
    logger.atInfo().log(builder.toString());
  }

  @Override
  public Boolean hasAnnotationErrors() {
    return errors.containsKey(ErrorType.ANNOTATION_EMPTY)
        || errors.containsKey(ErrorType.ANNOTATION_COMPLEX)
        || errors.containsKey(ErrorType.ANNOTATION_TYPE);
  }

  @Override
  public Boolean hasFidelityParamsErrors() {
    return errors.containsKey(ErrorType.FIDELITY_PARAMS_EMPTY)
        || errors.containsKey(ErrorType.FIDELITY_PARAMS_COMPLEX)
        || errors.containsKey(ErrorType.FIDELITY_PARAMS_TYPE);
  }
}
