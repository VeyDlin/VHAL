#include "ProbeLink.h"
#include <Drivers/Interface/Modbus/ModbusServer.h>
#include <Drivers/Interface/Modbus/RTU/RtuFramer.h>
#include <Utilities/Data/RegisterMap/RegisterView.h>


// Coils only, with the default MaxCoilWrite of 1968: function codes 01, 05 and 0F.
extern bool probeCoil;


using View = RegisterView<+[] {
	return RegisterViewBuilder()
		.Coils(0x0000, Variable(&probeCoil));
}>;


using Server = ModbusServer<ProbeLink, RtuFramer, View, +[] {
	return ServerBuilder()
		.SlaveAddress(1);
}>;


Server probeServer(probeLink);


void ProbeRun() {
	probeServer.Process();
}
