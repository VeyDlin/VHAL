// expect: endpoint address bits 6..4 are reserved
#include <Drivers/Interface/USB/Descriptor/USBConfigurationDescriptor.h>

constexpr std::array<uint8, 9> interfaceBytes = MakeUSBInterfaceDescriptor(0, 0, 0xFF, 0, 0, 0);
constexpr std::array<uint8, 7> endpointBytes = { 7, 5, 0x11, 0x02, 64, 0, 0 };
constexpr std::array<uint8, 25> configuration = MakeUSBConfigurationDescriptor(1, 0, false, false, 50, interfaceBytes, endpointBytes);
