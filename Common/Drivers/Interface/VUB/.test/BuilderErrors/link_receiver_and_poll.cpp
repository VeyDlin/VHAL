// expect: a link has both SetReceiver and Poll
#include "SnippetSupport.h"


// A packet link that is given a receiver and also polled for packets: two writers of the same receive queue.
struct BothLink {
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().Framed().MaxPacket(64).FullDuplex();

	void SetReceiver(VUB::Receiver receiver) {
		(void)receiver;
	}

	void Poll(VUB::Receiver receiver) {
		(void)receiver;
	}

	ResultStatus Send(std::span<const uint8> packet) {
		(void)packet;
		return ResultStatus::ok;
	}
};


using Stack = VUB::Stack<+[] {
	return VUB::Builder()
		.Link<BothLink>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 1);
