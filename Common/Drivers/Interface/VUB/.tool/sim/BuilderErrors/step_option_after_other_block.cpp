// expect: has no member named 'First'
#include "SnippetSupport.h"


// The option methods of a block are callable only right after it: a transform between the block and First is another block.
using Stack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<StepLayer>()
		.Transform<XorTransform>()
		.First(5)
		.Link<VUB::PacketEnd<64, 16>>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 3);
