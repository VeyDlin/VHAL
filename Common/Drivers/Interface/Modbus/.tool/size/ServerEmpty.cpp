#include "ProbeLink.h"
#include <Drivers/Interface/Modbus/ModbusServer.h>
#include <Drivers/Interface/Modbus/RTU/RtuFramer.h>
#include <Utilities/Data/RegisterMap/RegisterView.h>


// The server with no entries: the framing, Process and the exception replies. No function code is enabled.
using View = RegisterView<+[] {
	return RegisterViewBuilder();
}>;


using Server = ModbusServer<ProbeLink, RtuFramer, View, +[] {
	return ServerBuilder()
		.SlaveAddress(1);
}>;


Server probeServer(probeLink);


void ProbeRun() {
	probeServer.Process();
}
