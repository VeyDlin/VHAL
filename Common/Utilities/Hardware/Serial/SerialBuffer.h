#pragma once
#include <VHAL.h>
#include <Utilities/Buffer/RingBuffer.h>


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


	ResultStatus Clear() {
		buffer.Clear();
		return ResultStatus::ok;
	}


	inline bool IsReady() {
		return buffer.IsReady();
	}


	inline uint16 Size() {
		return buffer.Size();
	}


	Result<uint8> Pop() {
		return buffer.Pop();
	}
};
