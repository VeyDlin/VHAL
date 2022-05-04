#pragma once
#include <BSP/BSP.h>
#include <Utilities/Data/FIFO/StaticFifo.h>



template <size_t elementsCount>
class SerialFIFO {
private:
	StaticFifo<1, elementsCount> fifo;
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


	Status::statusType Clear() {
		return fifo.Clear();
	}


	inline bool IsReady() {
		return fifo.IsReady();
	}


	inline size_t Count() {
		return fifo.GetCount();
	}


	Status::info<uint8> Pop() {
		return fifo.Pop<uint8>();
	}
};


