#pragma once
#include <VHAL.h>
#include <Utilities/Data/FIFO/StaticFifo.h>


template <size_t ElementsCount>
class SerialFIFO {
private:
	StaticFifo<1, ElementsCount> fifo;
	AUART *serial;

public:
	SerialFIFO(UARTAdapter &uart): serial(&uart)  {
		serial->onInterrupt = [this](auto type) {
			if(type == AUART::Irq::Rx) {
				uint8 data = static_cast<uint8>(serial->GetLastRxData());
				fifo.Push<uint8>(data);
			}
		};
	}


	ResultStatus Clear() {
		return fifo.Clear();
	}


	inline bool IsReady() {
		return fifo.IsReady();
	}


	inline size_t Count() {
		return fifo.GetCount();
	}


	Result<uint8> Pop() {
		return fifo.Pop();
	}
};