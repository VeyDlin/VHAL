#include "ProbeUart.h"
#include <Drivers/Interface/Modbus/RTU/RtuTransport.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>


// The RTU transport with the default ring of 256 entries, two bytes each, and no pins. The clock is declared and not defined.
uint32 ProbeClock();


using Transport = RtuTransport<ProbeUart, ProbeGpio, +[] {
	return RtuBuilder()
		.Clock<&ProbeClock>();
}>;


Transport probeTransport(probeUart);


void ProbeRun() {
	std::array<uint8, 4> frame{ 1, 2, 3, 4 };
	std::array<uint8, 256> buffer{};
	probeTransport.Begin();
	probeTransport.Send(frame, 1000);
	Result<uint16> received = probeTransport.Receive(buffer, 1000);
	(void)received;
}
