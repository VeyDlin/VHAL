// expect: Lock() in the builder needs a lock type
#include "../ScriptedLink.h"
#include <Drivers/Interface/Modbus/ModbusClient.h>
#include <Drivers/Interface/Modbus/RTU/RtuFramer.h>


using Plc = ModbusClient<ScriptedLink, RtuFramer, +[] {
	return ClientBuilder()
		.SlaveAddress(1)
		.Lock();
}>;


static_assert(Plc::settings.locked);
