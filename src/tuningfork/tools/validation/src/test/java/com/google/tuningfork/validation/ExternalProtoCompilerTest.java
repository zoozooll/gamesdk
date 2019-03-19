package com.google.tuningfork.validation;

import static com.google.common.truth.Truth.assertThat;
import static java.nio.charset.StandardCharsets.UTF_8;
import static org.junit.Assert.assertThrows;

import com.google.common.collect.Lists;
import com.google.devtools.build.runtime.Runfiles;
import com.google.protobuf.Descriptors.Descriptor;
import com.google.protobuf.Descriptors.FileDescriptor;
import java.io.File;
import java.util.Arrays;
import java.util.List;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)
public final class ExternalProtoCompilerTest {
  @Rule
  // Override default behavior to allow overwriting files.
  public TemporaryFolder tempFolder =
      new TemporaryFolder() {
        @Override
        public File newFile(String filename) {
          return new File(getRoot(), filename);
        }
      };

  private static final File PROTOC_BINARY =
      Runfiles.location("net/proto2/compiler/public/protocol_compiler");

  private static List<String> defaultCommandLine;
  private final ExternalProtoCompiler compiler = new ExternalProtoCompiler(PROTOC_BINARY);
  private final TestdataHelper helper = new TestdataHelper(tempFolder);

  @Before
  public void setUp() {
    defaultCommandLine = Lists.newArrayList();
    defaultCommandLine.add(PROTOC_BINARY.getAbsolutePath());
    defaultCommandLine.add("-o");
    defaultCommandLine.add("/dev/stdout");
  }

  @Test
  public void compileValid() throws Exception {
    File file = helper.getFile("compile_valid.proto");

    FileDescriptor fDesc = compiler.compile(file);

    Descriptor messageDesc = fDesc.findMessageTypeByName("Message");
    Descriptor anotherDesc = fDesc.findMessageTypeByName("AnotherMessage");
    assertThat(messageDesc).isNotNull();
    assertThat(anotherDesc).isNotNull();
  }

  @Test
  public void compileInvalid() throws Exception {
    File file = helper.getFile("compile_invalid.proto");
    CompilationException expected =
        assertThrows(CompilationException.class, () -> compiler.compile(file));

    assertThat(expected)
        .hasMessageThat()
        .isEqualTo("Descriptor for [compile_invalid.proto] does not exist.");
  }

  @Test
  public void compileWithDeps() throws Exception {
    File file = helper.getFile("compile_with_deps.proto");
    CompilationException expected =
        assertThrows(CompilationException.class, () -> compiler.compile(file));

    assertThat(expected)
        .hasMessageThat()
        .isEqualTo("Descriptor for [compile_with_deps.proto] does not exist.");
  }

  @Test
  public void runEchoCommand() throws Exception {
    String expected = "Hello world";
    String result = new String(compiler.runCommand(Arrays.asList("echo", expected)), UTF_8);
    assertThat(result).startsWith(expected);
  }
}
