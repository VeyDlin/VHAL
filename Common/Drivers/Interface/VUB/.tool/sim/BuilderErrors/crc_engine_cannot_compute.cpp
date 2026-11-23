// expect: the engine cannot compute this CRC
#include "SnippetSupport.h"
#include <Drivers/Interface/VUB/Transform/VubCrc.h>


// An engine that computes CRC-16/MODBUS only, as a hardware unit might. It declares that through Supports.
struct ModbusOnlyEngine {
	static constexpr bool synchronous = true;


	template<typename Algorithm>
	static constexpr bool Supports() {
		const VubCrcParameters& parameters = Algorithm::parameters;
		return parameters.width == 16 && parameters.polynomial == 0x8005 && parameters.initialValue == 0xFFFF && parameters.finalXor == 0 &&
			parameters.reflectIn && parameters.reflectOut;
	}


	template<typename Algorithm>
	static constexpr uint32 Run(std::span<const uint8> data) {
		(void)data;
		return 0;
	}
};


// CRC-8/SMBUS is not something this engine computes, so the builder refuses the chain.
using Stack = Vub<+[] {
	return VubBuilder()
		.Transform<VubCrc<VubCrc8Smbus::parameters, ModbusOnlyEngine>>()
		.Link<VubPacketEnd<64, 16>>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 2);
