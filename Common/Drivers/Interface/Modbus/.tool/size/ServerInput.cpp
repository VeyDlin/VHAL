#include "ProbeLink.h"
#include <Drivers/Interface/Modbus/ModbusServer.h>
#include <Drivers/Interface/Modbus/RTU/RtuFramer.h>
#include <Utilities/Data/RegisterMap/RegisterView.h>


// Input registers only: function code 04.
uint16 ProbeInputRegister();


using View = RegisterView<+[] {
	return RegisterViewBuilder()
		.InputRegisters(0x0000, Computed(&ProbeInputRegister));
}>;


using Server = ModbusServer<ProbeLink, RtuFramer, View, +[] {
	return ServerBuilder()
		.SlaveAddress(1);
}>;


Server probeServer(probeLink);


void ProbeRun() {
	probeServer.Process();
}
