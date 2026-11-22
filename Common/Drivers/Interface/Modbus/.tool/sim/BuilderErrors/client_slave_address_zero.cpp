// expect: slave address must be 1 to 247 (0 is broadcast: use Broadcast())
#include "../ScriptedLink.h"
#include <Drivers/Interface/Modbus/ModbusClient.h>
#include <Drivers/Interface/Modbus/RTU/RtuFramer.h>


using Plc = ModbusClient<ScriptedLink, RtuFramer, +[] {
	return ClientBuilder()
		.SlaveAddress(0);
}>;


static_assert(Plc::settings.slaveAddress == 0);
