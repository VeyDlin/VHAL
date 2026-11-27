// expect: bInterval of a full-speed isochronous endpoint is 1
#include <Drivers/Interface/USB/Descriptor/USBConfigurationDescriptor.h>

constexpr std::array<uint8, 9> interfaceBytes = MakeUSBInterfaceDescriptor(0, 0, 0xFF, 0, 0, 0);
constexpr std::array<uint8, 9> endpointBytes = { 9, 5, 0x81, 0x01, 64, 0, 2, 0, 0 };
constexpr std::array<uint8, 27> configuration = MakeUSBConfigurationDescriptor(1, 0, false, false, 50, interfaceBytes, endpointBytes);
