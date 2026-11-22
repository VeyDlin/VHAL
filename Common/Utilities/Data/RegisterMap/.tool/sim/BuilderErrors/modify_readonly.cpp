// expect: Modify<Field>() needs ReadWrite access
#include <Utilities/Data/RegisterMap/BitField.h>
#include <Utilities/Data/RegisterMap/RegisterAccess.h>
#include <span>


struct FakeBus {
	ResultStatus ReadRegs(RegisterRef, std::span<uint8>) {
		return ResultStatus::ok;
	}


	ResultStatus WriteRegs(RegisterRef, std::span<const uint8>) {
		return ResultStatus::ok;
	}
};


constexpr auto Status = RegisterDescriptor<uint8>(0x09, 0x02).ReadOnly();
constexpr auto StatusBit = BitField<1>(Status, 0);


int main() {
	FakeBus bus;
	Modify<StatusBit>(bus, true);
	return 0;
}
