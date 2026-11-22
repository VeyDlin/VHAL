// expect: bit field is outside the register width
#include <Utilities/Data/RegisterMap/BitField.h>
#include <Utilities/Data/RegisterMap/RegisterDescriptor.h>


constexpr auto Status = BitField<2>(RegisterDescriptor<uint8>(0x09, 0x02), 7);


int main() {
	return 0;
}
