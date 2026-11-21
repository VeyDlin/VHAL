// expect: full-speed isochronous packets are 1 to 1023 bytes
#include <Drivers/Interface/USB/Descriptor/USBConfigurationDescriptor.h>

constexpr std::array<uint8, 7> endpointBytes = MakeUSBEndpointDescriptor(0x81, USBEndpointType::Isochronous, 1024, 1);
