#include "ProbeLink.h"
#include <Drivers/Interface/Modbus/ModbusServer.h>
#include <Drivers/Interface/Modbus/RTU/RtuFramer.h>
#include <Utilities/Data/RegisterMap/RegisterView.h>


// Every entry kind: function codes 01 to 06, 0F and 10.
extern uint16 probeHolding;
uint16 ProbeInputRegister();
extern bool probeCoil;
bool ProbeDiscreteInput();


using View = RegisterView<+[] {
	return RegisterViewBuilder()
		.HoldingRegisters(0x0000, Variable(&probeHolding))
		.InputRegisters(0x0000, Computed(&ProbeInputRegister))
		.Coils(0x0000, Variable(&probeCoil))
		.DiscreteInputs(0x0000, Computed(&ProbeDiscreteInput));
}>;


using Server = ModbusServer<ProbeLink, RtuFramer, View, +[] {
	return ServerBuilder()
		.SlaveAddress(1);
}>;


Server probeServer(probeLink);


void ProbeRun() {
	probeServer.Process();
}
