// expect: device address must be 7 bits
#include <Utilities/Data/RegisterMap/RegisterDescriptor.h>


constexpr auto Control = RegisterDescriptor<uint8>(0x80, 0x01);


int main() {
	return 0;
}
