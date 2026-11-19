// expect: the 9-byte endpoint form is bulk
#include <Drivers/Interface/USB/Descriptor/USBConfigurationDescriptor.h>

constexpr std::array<uint8, 9> endpointBytes = MakeUSBAudioEndpointDescriptor(0x83, USBEndpointType::Interrupt, 8, 1);
