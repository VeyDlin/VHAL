// expect: the layer cannot be built from its options
#include "SnippetSupport.h"


// TestLayer is built from TestLayerOptions, so an unsigned value is not an options value of the layer.
using Stack = Vub<+[] {
	return VubBuilder()
		.Layer<TestLayer>(7u)
		.Link<VubPacketEnd<64, 16>>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 2);
