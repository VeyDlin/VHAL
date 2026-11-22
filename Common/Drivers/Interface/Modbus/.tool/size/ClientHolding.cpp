#include "ProbeLink.h"
#include <Drivers/Interface/Modbus/ModbusClient.h>
#include <Drivers/Interface/Modbus/RTU/RtuFramer.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>


// The client that reads holding registers only, through the array form. Each other function is not reached, so it is not in the object.
using Client = ModbusClient<ProbeLink, RtuFramer, +[] {
	return ClientBuilder()
		.SlaveAddress(1)
		.Timeout(500)
		.Retries(2);
}>;


Client probeClient(probeLink);


void ProbeRun() {
	std::array<uint16, 4> values{};
	ModbusResult result = probeClient.ReadHoldingRegisters(0x0000, values);
	(void)result;
}
