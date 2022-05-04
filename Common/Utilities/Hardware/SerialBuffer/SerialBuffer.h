#pragma once
#include <BSP.h>
#include <Utilities/Data/Buffer/RingBuffer.h>
#include <Adapter/OSAdapter/RTOS.h>
#include <Adapter/OSAdapter/CriticalSection.h>


template <uint16 BufferSize>
class SerialBuffer {
private:
	RingBuffer<uint8, BufferSize> buffer;
	AUART *serial;


public:
	SerialBuffer(UARTAdapter &uart): serial(&uart)  {
		serial->onInterrupt = [this](auto type) {
			if(type == AUART::Irq::Rx) {
				buffer.Push(serial->GetLastRxData());
			}
		};
	}


	Status::statusType Clear() {
		return buffer.Clear();
	}


	inline bool IsReady() {
		return buffer.IsReady();
	}


	inline uint16 Size() {
		return buffer.Size();
	}


	Status::info<uint8> Pop() {
		Status::info<uint8> out;

		out.type = buffer.Pop(out.data);

		return out;
	}
};


