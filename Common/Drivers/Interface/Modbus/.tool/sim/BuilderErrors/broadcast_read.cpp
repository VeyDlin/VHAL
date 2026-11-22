// expect: has no member named
#include "../ScriptedLink.h"
#include <Drivers/Interface/Modbus/ModbusClient.h>
#include <Drivers/Interface/Modbus/RTU/RtuFramer.h>
#include <array>


using Plc = ModbusClient<ScriptedLink, RtuFramer, +[] {
	return ClientBuilder()
		.SlaveAddress(1);
}>;


void Probe(Plc& plc) {
	std::array<uint16, 2> values{};
	plc.Broadcast().ReadHoldingRegisters(0x0000, values);
}
