# This script parses the logcat lines produced by the Tuning Fork DebugBackend
#  which are base64 encoded serializations of TuningForkLogEvent protos.
# Usage:
#  adb logcat -d | python parselogcat.py

import sys
import re

# To generate these from the proto files:
# cd ../../src/tuningfork/proto
# protoc --python_out=../../../samples/tuningfork -I. tuningfork.proto
# protoc --python_out=../../../samples/tuningfork -I. eng_tuningfork.proto
# protoc --python_out=../../../samples/tuningfork -I. tuningfork_clearcut_log.proto
import tuningfork_clearcut_log_pb2 as tcl
import eng_tuningfork_pb2 as tf

# Example logcat line:
#11-30 15:32:22.892 13781 16553 I TuningFork: (TCL1/1)GgAqHAgAEgAaFgAAAAAAAAAAAAAAAAAAAAAAAAAAAEg=
tflogcat_regex = r"(\S+ \S+).*TuningFork: \(TCL(.+)/(.+)\)(.*)"

ser = ""
def getTCLEvent(i, n, ser_in):
  global ser
  if i==1:
    ser = ""
  ser += ser_in
  if i<>n:
    return
  l = tcl.TuningForkLogEvent()
  l.ParseFromString(ser.decode("base64"))
  return l

def readStdin():
  for logcat_lines in sys.stdin.readlines():
    m = re.match(tflogcat_regex, logcat_lines)
    if m:
      subparts = m.groups()
      tstamp = subparts[0]
      tclevent = getTCLEvent(int(subparts[1]),int(subparts[2]),subparts[3])
      print tclevent

def main():
  readStdin()

if __name__ == "__main__":
  main()
