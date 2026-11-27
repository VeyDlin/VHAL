// expect: a chain needs a link
#include "SnippetSupport.h"


using Stack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<TestLayer>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 1);
