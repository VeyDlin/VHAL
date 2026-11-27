// expect: USB: a MIDI endpoint lists 1 to 16 embedded jacks
#include <Drivers/Interface/USB/Class/MIDI/USBMidiBuilder.h>

constexpr std::array<uint8, USBMidiMaxCables> jackIds{};
constexpr auto jacks = MakeUSBMidiEndpointJacks(jackIds, 0);
