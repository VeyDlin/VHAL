#pragma once
#include <VHAL.h>
#include <Utilities/Buffer/StaticFifo.h>


template <size_t ElementsCount>
class SerialFIFO {
private:
	StaticFifo<ElementsCount, uint8> fifo;
	AUART *serial;

public:
	SerialFIFO(AUART &uart): serial(&uart)  {
		serial->SetOnInterrupt([this](AUART::Irq type) {
			if(type == AUART::Irq::Rx) {
				uint8 data = static_cast<uint8>(serial->GetLastRxData());
				fifo.Push(data);
			}
		});
	}


	ResultStatus Clear() {
		return fifo.Clear();
	}


	inline bool IsReady() {
		// StaticFifo::IsReady() reports "not busy", which is true for an empty queue too.
		return fifo.GetCount() != 0;
	}


	inline size_t Count() {
		return fifo.GetCount();
	}


	Result<uint8> Pop() {
		return fifo.Pop();
	}
};