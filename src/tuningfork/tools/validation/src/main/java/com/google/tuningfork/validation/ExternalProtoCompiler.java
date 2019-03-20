// Copyright 2009 Google Inc. All Rights Reserved.

package com.google.tuningfork.validation;

import com.google.common.base.Preconditions;
import com.google.common.collect.ImmutableList;
import com.google.protobuf.DescriptorProtos.FileDescriptorProto;
import com.google.protobuf.DescriptorProtos.FileDescriptorSet;
import com.google.protobuf.Descriptors.DescriptorValidationException;
import com.google.protobuf.Descriptors.FileDescriptor;
import com.google.protobuf.InvalidProtocolBufferException;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.util.List;

/** Compiles .proto file into a {@link FileDescriptor} */
public class ExternalProtoCompiler {

  private final String protoPath;
  private final FileDescriptor[] emptyDeps = new FileDescriptor[0];

  public ExternalProtoCompiler(File protoBinary) {
    Preconditions.checkNotNull(protoBinary, "executable");
    protoPath = protoBinary.getAbsolutePath();
  }

  public FileDescriptor compile(File file) throws IOException, CompilationException {
    Preconditions.checkNotNull(file, "file");
    FileDescriptor descriptor = buildAndRunCompilerProcess(file);
    return descriptor;
  }

  public byte[] runCommand(List<String> commandLine) throws IOException, CompilationException {
    Process process = new ProcessBuilder(commandLine).start();
    try {
      process.waitFor();
    } catch (InterruptedException e) {
      throw new CompilationException("Process was interrupted", e);
    }
    InputStream stdin = process.getInputStream();
    byte[] result = new byte[stdin.available()];
    stdin.read(result);
    return result;
  }

  private FileDescriptor buildAndRunCompilerProcess(File file)
      throws IOException, CompilationException {
    List<String> commandLine = createCommandLine(file);
    byte[] result = runCommand(commandLine);

    try {
      FileDescriptorSet fileSet = FileDescriptorSet.parseFrom(result);
      for (FileDescriptorProto descProto : fileSet.getFileList()) {
        if (descProto.getName().equals(file.getName())) {
          return FileDescriptor.buildFrom(descProto, emptyDeps);
        }
      }
    } catch (DescriptorValidationException | InvalidProtocolBufferException e) {
      throw new IllegalStateException(e);
    }
    throw new CompilationException(
        String.format("Descriptor for [%s] does not exist.", file.getName()));
  }

  private List<String> createCommandLine(File file) {
    return ImmutableList.of(
        protoPath,
        "-o",
        "/dev/stdout",
        "-I",
        file.getName() + "=" + file.getAbsolutePath(), // That should be one line
        file.getName());
  }
}
