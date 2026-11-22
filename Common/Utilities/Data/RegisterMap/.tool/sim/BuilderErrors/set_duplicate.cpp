// expect: two registers share a table, device and address
#include <Utilities/Data/RegisterMap/RegisterSet.h>


constexpr auto First = RegisterDescriptor<uint8>(0x09, 0x01);
constexpr auto Second = RegisterDescriptor<uint16>(0x09, 0x01);


constexpr RegisterSet<First, Second> board{};


int main() {
	return 0;
}
