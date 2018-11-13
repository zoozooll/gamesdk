#include "device_info.h"

int main(){
  device_info::root proto = device_info::createProto();

  std::cout << "*Proto debug begin:" << std::endl;
  proto.PrintDebugString();
  std::cout << "*Proto debug end." << std::endl;

  std::cout << "fin." << std::endl;
  return 0;
}
