// expect: the chain has more than one block of this type
#include "SnippetSupport.h"


// Find names one block by its type, so a chain with two blocks of the type has no block to pick.
using Stack = Vub<+[] {
	return VubBuilder()
		.Layer<StepLayer>()
		.Layer<StepLayer>()
		.Link<VubPacketEnd<64, 16>>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


inline void UseFind(Stack& stack) {
	(void)stack.Find<StepLayer>();
}


static_assert(Stack::count == 3);
