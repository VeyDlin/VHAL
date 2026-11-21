// expect: bInterval of a full-speed isochronous endpoint is 1
#include <Drivers/Interface/USB/Descriptor/USBConfigurationDescriptor.h>

constexpr std::array<uint8, 7> endpointBytes = MakeUSBEndpointDescriptor(0x81, USBEndpointType::Isochronous, 192, 4);
