// expect: turnaround must be under 3.5 character times
#include <Drivers/Interface/Modbus/RTU/RtuTransport.h>


uint32 SnippetClock() {
	return 0;
}


using Link = RtuTransport<int, int, +[] {
	return RtuBuilder()
		.Turnaround(4)
		.Clock<&SnippetClock>();
}>;


static_assert(Link::settings.turnaroundCharacters == 4);
