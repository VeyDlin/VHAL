// expect: register is read-only: Write<Reg>() needs Write or ReadWrite access
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


int main() {
	FakeBus bus;
	Write<Status>(bus, 1);
	return 0;
}
