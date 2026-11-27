// expect: a WebUSB URL is at most 252 bytes after its scheme
#include <Drivers/Interface/USB/Descriptor/WebUsb/USBWebUsbUrlDescriptor.h>

constexpr std::array<uint8, 3> tooLong = MakeWebUsbUrlDescriptor<"https://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa">();
