#pragma once
#include <Utilities/DataTypes.h>


// Modbus RTU frame gaps in microseconds. A character is 11 bits on the wire (start, 8 data, parity or not, stop), whatever the port
// is set to, so the parity and the stop bits do not change the gaps. Above 19200 baud the specification fixes the two gaps
// (Modbus over Serial Line, V1.02, section 2.5.1.1). Every value is in uint32: the largest product, 165 * 100000, is far below 2^32,
// so no gap wraps at a low baud rate.
constexpr uint32 RtuFixedBaudLimit = 19200;
constexpr uint32 RtuFixedInterCharacterUs = 750;
constexpr uint32 RtuFixedInterFrameUs = 1750;


// The gaps of one baud rate. interCharacterUs is t1.5 and interFrameUs is t3.5. The baud rate must be above 0.
struct RtuTiming {
	uint32 interCharacterUs;
	uint32 interFrameUs;
};


// The gaps of a baud rate: t1.5 is 16.5 bit times and t3.5 is 38.5 bit times, truncated to whole microseconds.
constexpr RtuTiming RtuTimingOf(uint32 baud) {
	if (baud > RtuFixedBaudLimit) {
		return RtuTiming{RtuFixedInterCharacterUs, RtuFixedInterFrameUs};
	}

	return RtuTiming{165 * 100000 / baud, 385 * 100000 / baud};
}


// The time of count characters at baud, in microseconds. Used for the turnaround hold, which is measured in characters.
constexpr uint32 RtuCharactersUs(uint32 baud, uint32 count) {
	return count * 11000000 / baud;
}
