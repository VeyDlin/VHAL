#include <Utilities/Data/RegisterMap/RegisterAccess.h>
#include <span>


// The -Os check: a descriptor that nothing uses leaves no symbol in the object file. The name UnusedProbeDescriptor must not
// appear in nm's output. UsedProbeDescriptor is used by main; it folds into the call and is not checked by name.


constexpr auto UnusedProbeDescriptor = RegisterDescriptor<uint16>(0x68, 0x3B).ReadWrite();
constexpr auto UsedProbeDescriptor = RegisterDescriptor<uint8>(0x09, 0x01).ReadWrite();


struct ProbeBus {
	ResultStatus ReadRegs(RegisterRef, std::span<uint8> in) {
		in[0] = 0;
		return ResultStatus::ok;
	}


	ResultStatus WriteRegs(RegisterRef, std::span<const uint8>) {
		return ResultStatus::ok;
	}
};


int main(int argc, char**) {
	ProbeBus bus;
	Write<UsedProbeDescriptor>(bus, static_cast<uint8>(argc));
	return 0;
}
