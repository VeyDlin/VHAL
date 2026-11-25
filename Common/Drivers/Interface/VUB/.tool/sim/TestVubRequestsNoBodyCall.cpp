#include "SimCheck.h"
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/LinkTraits.h>
#include <Drivers/Interface/VUB/Requests.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>
#include <utility>

// A command with no body is sent without an argument: Fire<Id>() and Send<Id, OnFinal>() give the body NoBody, as Emit and Await do.


enum class NoBodyStatus : uint8 {
	done = 0
};


enum class NoBodyCommand : uint8 {
	poke = 0x21,
	stop = 0x22
};


using NoBodyCommands = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<NoBodyCommand::poke>()
			.Fire()
		.Command<NoBodyCommand::stop>()
			.Reply<NoBodyStatus>()
			.ReplyTimeoutMs(2000);
}>;


// A byte-stream link that keeps the bytes it is given.
class NoBodyStream {
public:
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().MaxPacket(64).HalfDuplex();


	Result<std::size_t> Write(std::span<const uint8> bytes) {
		for (std::size_t index = 0; index < bytes.size(); index++) {
			if (count < wire.size()) {
				wire[count] = bytes[index];
			}

			count++;
		}

		return bytes.size();
	}


	std::array<uint8, 128> wire{};
	std::size_t count = 0;
};


void NoBodyFinal(const VUB::Final<NoBodyStatus, VUB::NoBody>&) {
}


void NoBodyFill(VUB::NoBody&) {
}


uint32 NoBodyNow() {
	return 0;
}


void NoBodyReceive(const VUB::Meta&, std::span<const uint8>) {
}


using NoBodyStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<VUB::Requests<NoBodyCommands, VUB::Handlers<>>>().Open(2)
		.Framing<VUB::Cobs>()
		.Link<NoBodyStream>()
		.MaxPayload(32)
		.OnReceive<&NoBodyReceive>()
		.Clock<&NoBodyNow>();
}>;


using NoBodyHandle = decltype(std::declval<NoBodyStack&>().Requests());


// Compile checks: the argument-less forms exist for a NoBody command, and a command with a body has only the forms with an argument.
template<typename Handle>
concept HasNoBodySend = requires(const Handle& handle) {
	handle.template Send<NoBodyCommand::stop, &NoBodyFinal>();
};


template<typename Handle>
concept HasNoBodyFire = requires(const Handle& handle) {
	handle.template Fire<NoBodyCommand::poke>();
};


static_assert(HasNoBodySend<NoBodyHandle>, "Send of a NoBody command has no argument form");
static_assert(HasNoBodyFire<NoBodyHandle>, "Fire of a NoBody command has no argument form");


// Fire of a command with no body sends its packet at once, with no slot and no outcome.
void TestFireWithoutArgumentSends() {
	NoBodyStream link;
	NoBodyStack stack(link);
	NoBodyHandle handle = stack.Requests();
	SIM_EXPECT(handle.Fire<NoBodyCommand::poke>() == ResultStatus::ok);
	SIM_EXPECT(link.count > 0);
}


int main() {
	TestFireWithoutArgumentSends();
	return SimFinish("TestVubRequestsNoBodyCall");
}
