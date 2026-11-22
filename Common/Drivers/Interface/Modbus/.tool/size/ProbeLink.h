#pragma once
#include <Drivers/Interface/Modbus/Transport.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <span>


// The transport of the server and client probes. Its functions are declared and not defined, so each probe is an object file that
// holds the code the probe reaches and nothing else. The section sizes of that object are the measured cost of the probe.
struct ProbeLink {
	ResultStatus Begin();
	ResultStatus Send(std::span<const uint8> frame, uint32 timeoutUs);
	Result<uint16> Receive(std::span<uint8> frame, uint32 timeoutUs);
};


// The one transport object of the probes. No probe defines it, so its storage is not in the measured sections.
extern ProbeLink probeLink;
