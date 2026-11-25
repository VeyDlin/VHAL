// expect: Send from another task needs StageQueue(n)
#include "SnippetSupport.h"


// A concurrent stack without StageQueue(n) builds, and its layers get the flag. Its Send from another task is a compile error.
using Stack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<TestLayer>()
		.Link<VUB::PacketEnd<64, 16>>()
		.MaxPayload(40)
		.Concurrent()
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


inline void SnippetSend(Stack& stack) {
	std::array<uint8, 2> payload = {0, 1};
	(void)stack.Send(std::span<const uint8>(payload.data(), payload.size()));
}
