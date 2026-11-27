#pragma once
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <functional>


// The UART and the pin of the RTU transport probe, with the members the transport calls. Declared and not defined, like ProbeLink.
struct ProbeUart {
	enum class Irq { Tx, Rx };
	enum class Error { None, Parity, Noise, Frame, Overrun };


	struct Parameters {
		uint32 baudRate = 9600;
	};


	const Parameters& GetParameters() const;
	ResultStatus SetContinuousAsyncRxMode(bool mode);
	void SetOnInterrupt(std::function<void(Irq)> handler);
	void SetOnError(std::function<void(Error)> handler);
	ResultStatus WriteArray(const uint8* buffer, uint32 size);
	uint16 GetLastRxData();
};


struct ProbeGpio {
	void Set();
	void Reset();
};


extern ProbeUart probeUart;
