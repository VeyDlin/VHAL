// expect: sync type and usage are isochronous only
#include <Drivers/Interface/USB/Descriptor/USBConfigurationDescriptor.h>

constexpr std::array<uint8, 7> endpointBytes = MakeUSBEndpointDescriptor(0x81, USBEndpointType::Bulk, 64, 0, USBIsoSync::Asynchronous);
