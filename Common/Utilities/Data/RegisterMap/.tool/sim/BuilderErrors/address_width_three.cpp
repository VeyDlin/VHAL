// expect: address width must be 1 or 2 bytes
#include <Utilities/Data/RegisterMap/RegisterDescriptor.h>


constexpr auto Control = RegisterDescriptor<uint8>(0x09, 0x01).AddressWidth(3);


int main() {
	return 0;
}
