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

import static com.google.common.base.Preconditions.checkArgument;

import com.google.common.base.Strings;
import com.google.common.flags.Flag;
import com.google.common.flags.FlagSpec;
import com.google.common.flags.Flags;
import com.google.common.flogger.GoogleLogger;
import java.io.File;
import java.io.IOException;
import java.util.Enumeration;
import java.util.jar.JarEntry;
import java.util.jar.JarFile;

/** APK Validation tool for Tuningfork */
final class TuningforkApkValidationTool {
  private static final GoogleLogger logger = GoogleLogger.forEnclosingClass();

  @FlagSpec(help = "Path to apk file")
  public static final Flag<String> apkPath = Flag.nullString();

  @FlagSpec(help = "Path to proto compiler")
  public static final Flag<String> protoCompiler = Flag.nullString();

  public static void main(String[] args) {
    Flags.parse(args);

    checkArgument(
        !Strings.isNullOrEmpty(apkPath.get()),
        "You need to specify path to your apk file --apkPath");

    checkArgument(
        !Strings.isNullOrEmpty(protoCompiler.get()),
        "You need to specify path to proto compiler --protoCompiler");

    File apkFile = new File(apkPath.get());
    if (!apkFile.exists()) {
      logger.atSevere().log("APK File does not exist %s", apkPath.get());
      return;
    }

    File protoCompilerFile = new File(protoCompiler.get());
    if (!protoCompilerFile.exists()) {
      logger.atSevere().log("Proto compiler file does not exist %s", protoCompiler.get());
      return;
    }

    logger.atInfo().log("Start validation of %s...", apkFile.getName());

    JarFile jarApk;

    try {
      jarApk = new JarFile(apkFile);
    } catch (IOException e) {
      logger.atSevere().withCause(e).log("Can not open apk file %s", apkFile.getName());
      return;
    }

    ErrorCollector errors = new ParserErrorCollector();
    DeveloperTuningforkParser parser = new DeveloperTuningforkParser(errors, protoCompilerFile);

    Enumeration<JarEntry> apkFiles = jarApk.entries();
    while (apkFiles.hasMoreElements()) {
      JarEntry file = apkFiles.nextElement();
      try {
        parser.parseJarEntry(jarApk, file);
      } catch (Exception e) {
        logger.atWarning().withCause(e).log("Can not parse apk entry %s", file.getName());
      }
    }

    try {
      parser.validate();
      if (errors.getErrorCount() == 0) {
        logger.atInfo().log("Apk %s is valid", apkFile.getName());
      } else {
        errors.printStatus();
      }
    } catch (IOException | CompilationException e) {
      logger.atSevere().withCause(e).log("Error happen during validation");
    }
  }
}
