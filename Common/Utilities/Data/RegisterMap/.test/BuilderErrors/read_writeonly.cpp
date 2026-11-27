// expect: register is write-only: Read<Reg>() needs Read or ReadWrite access
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


constexpr auto Command = RegisterDescriptor<uint8>(0x09, 0x03).WriteOnly();


int main() {
	FakeBus bus;
	Read<Command>(bus);
	return 0;
}
