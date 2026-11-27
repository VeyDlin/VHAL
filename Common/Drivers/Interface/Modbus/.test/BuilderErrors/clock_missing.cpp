// expect: clock is missing: add Clock<&Fn>() to the RtuBuilder
#include <Drivers/Interface/Modbus/RTU/RtuTransport.h>


using Link = RtuTransport<int, int, +[] {
	return RtuBuilder()
		.Turnaround(2);
}>;


static_assert(Link::settings.turnaroundCharacters == 2);
