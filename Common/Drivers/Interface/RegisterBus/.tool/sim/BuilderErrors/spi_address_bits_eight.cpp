// expect: address bits are 1 to 7
#include <Drivers/Interface/RegisterBus/SpiRegisterBus.h>


constexpr SpiFramingBuilder framing = SpiFramingBuilder().AddressBits(8);


int main() {
	return 0;
}
