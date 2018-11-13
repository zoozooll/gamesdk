#include "device_info.pb.h"

#include <EGL/egl.h>
#include <GLES3/gl32.h>

#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <fstream>
#include <vector>
#include <set>

namespace device_info {
device_info::root createProto();
}  // namespace device_info
