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

import static java.nio.charset.StandardCharsets.UTF_8;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.LinkedListMultimap;
import com.google.common.collect.ListMultimap;
import com.google.common.flogger.FluentLogger;
import com.google.common.io.ByteStreams;
import com.google.protobuf.ByteString;
import com.google.protobuf.Descriptors.Descriptor;
import com.google.protobuf.Descriptors.FileDescriptor;
import com.google.tuningfork.Tuningfork.Settings;
import java.io.File;
import java.io.IOException;
import java.util.Optional;
import java.util.jar.JarEntry;
import java.util.jar.JarFile;

/** Tuningfork validation tool.
 * Parses proto and settings files and validates them. */
public class DeveloperTuningforkParser {

  private Optional<String> devTuningfork = Optional.empty();
  private Optional<ByteString> tuningforkSettings = Optional.empty();
  private final ListMultimap<String, ByteString> devFidelityParams = LinkedListMultimap.create();

  private final ErrorCollector errors;
  private final File protocBinary;

  private static final FluentLogger logger = FluentLogger.forEnclosingClass();

  public DeveloperTuningforkParser(ErrorCollector errors, File protocBinary) {
    this.errors = errors;
    this.protocBinary = protocBinary;
  }

  public void parseJarEntry(JarFile apk, JarEntry file) throws IOException {
    if (file.getName().equals(ApkConfig.DEV_TUNINGFORK_PROTO)) {
      String content = new String(ByteStreams.toByteArray(apk.getInputStream(file)), UTF_8);
      devTuningfork = Optional.of(content);
    } else if (file.getName().equals(ApkConfig.TUNINGFORK_SETTINGS)) {
      ByteString content = ByteString.readFrom(apk.getInputStream(file));
      tuningforkSettings = Optional.of(content);
    } else if (ApkConfig.DEV_FIDELITY_PATTERN.matcher(file.getName()).find()) {
      ByteString content = ByteString.readFrom(apk.getInputStream(file));
      devFidelityParams.put(file.getName(), content);
    }
  }

  /** Validate protos and settings.*/
  public void validate() throws IOException, CompilationException {

    FileDescriptor devTuningforkFileDesc =
        ProtoHelper.compileContent(devTuningfork.get(), protocBinary);
    Descriptor annotationField = devTuningforkFileDesc.findMessageTypeByName("Annotation");
    Descriptor fidelityField = devTuningforkFileDesc.findMessageTypeByName("FidelityParams");

    ImmutableList<Integer> enumSizes =
        ValidationUtil.validateAnnotationAndGetEnumSizes(annotationField, errors);
    ValidationUtil.validateFidelityParams(fidelityField, errors);

    logger.atInfo().log("Loaded Annotation message:\n" + annotationField.toProto());
    logger.atInfo().log("Loaded FidelityParams message:\n" + fidelityField.toProto());

    // Validate settings only if annotations are valid
    if (!errors.hasAnnotationErrors()) {
      ValidationUtil.validateSettings(enumSizes, tuningforkSettings.get(), errors);
      Settings settings = Settings.parseFrom(tuningforkSettings.get());
      logger.atInfo().log("Loaded settings:\n" + settings);
    }

    // Validate devFidelityOnly only if fidelity parameters are valid
    if (!errors.hasFidelityParamsErrors()) {
      ValidationUtil.validateDevFidelityParams(devFidelityParams.values(), fidelityField, errors);
    }
  }
}
