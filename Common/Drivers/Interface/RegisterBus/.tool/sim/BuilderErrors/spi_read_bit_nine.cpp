// expect: the read bit is 0 to 7
#include <Drivers/Interface/RegisterBus/SpiRegisterBus.h>


constexpr SpiFramingBuilder framing = SpiFramingBuilder().ReadBit(9);


int main() {
	return 0;
}
