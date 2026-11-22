#include "ProbeLink.h"
#include <Drivers/Interface/Modbus/ModbusServer.h>
#include <Drivers/Interface/Modbus/RTU/RtuFramer.h>
#include <Utilities/Data/RegisterMap/RegisterView.h>


// Coils only, with MaxCoilWrite(64): the bool buffer of function code 0F holds 64 coils.
extern bool probeCoil;


using View = RegisterView<+[] {
	return RegisterViewBuilder()
		.Coils(0x0000, Variable(&probeCoil));
}>;


using Server = ModbusServer<ProbeLink, RtuFramer, View, +[] {
	return ServerBuilder()
		.SlaveAddress(1)
		.MaxCoilWrite(64);
}>;


Server probeServer(probeLink);


void ProbeRun() {
	probeServer.Process();
}
