// expect: a chain with a bulk layer needs a send queue
#include "SnippetSupport.h"
#include <Drivers/Interface/VUB/Framing/Cobs.h>


// Bulk frames follow each other, so the application would never find the link free: a bulk chain has a send queue of at least one.
using Stack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<TestLayer>(TestLayerOptions{0, true, 0})
		.Framing<VUB::Cobs>()
		.Link<VUB::StreamEnd<64, 16>>()
		.MaxPayload(40)
		.SendQueue(0)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 3);
