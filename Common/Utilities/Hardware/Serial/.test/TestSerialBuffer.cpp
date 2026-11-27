#include <VHAL.h>
#include <Utilities/Hardware/Serial/SerialBuffer.h>
#include <VhalCheck.h>
#include <vector>


// Host tests of the serial receive buffer: the byte order across the wrap of the ring, and the overfill, which keeps the oldest bytes.


namespace {


void CheckWrapOrder() {
	AUART uart;
	SerialBuffer<4> buffer(uart);
	uart.Receive(1);
	uart.Receive(2);
	uart.Receive(3);
	uart.Receive(4);
	std::vector<uint8> popped;
	popped.push_back(buffer.Pop().Value());
	popped.push_back(buffer.Pop().Value());
	uart.Receive(5);
	uart.Receive(6);
	while (buffer.IsReady()) {
		popped.push_back(buffer.Pop().Value());
	}
	VHAL_CHECK(popped == std::vector<uint8>({ 1, 2, 3, 4, 5, 6 }));
}


void CheckFullDropsNewest() {
	AUART uart;
	SerialBuffer<4> buffer(uart);
	for (uint8 value = 1; value <= 5; ++value) {
		uart.Receive(value);
	}
	VHAL_CHECK_EQ(buffer.Size(), 4);
	std::vector<uint8> popped;
	while (buffer.IsReady()) {
		popped.push_back(buffer.Pop().Value());
	}
	VHAL_CHECK(popped == std::vector<uint8>({ 1, 2, 3, 4 }));
}


}


int main() {
	CheckWrapOrder();
	CheckFullDropsNewest();
	return VhalCheck::Finish("TestSerialBuffer");
}
