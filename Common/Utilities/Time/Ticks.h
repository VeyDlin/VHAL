#pragma once
#include <Utilities/DataTypes.h>
#include <concepts>


// Comparisons of free-running tick counters that hold across a wrap of the counter. A 32-bit counter of microseconds wraps about every
// 71 minutes, and a 32-bit counter of milliseconds about every 49 days. The uint32 forms take the difference of two values, so they
// are right across a wrap: Reached needs the two values to be less than 2^31 apart, and Elapsed holds for any interval below 2^32.
// The uint64 forms compare the values as they are, with no wrap.
namespace Ticks {


// The microseconds in a millisecond.
inline constexpr uint32 usPerMs = 1000;


// Whole seconds and the microseconds that remain, as a timeval holds them. The ports build their own timeval from it.
struct Split {
	uint32 seconds;
	uint32 microseconds;
};


// True when the counter has reached due. The difference is read as a signed 32-bit count, so the answer is right across a wrap when the
// two values are less than 2^31 apart.
constexpr bool Reached(uint32 now, uint32 due) {
	return static_cast<int32>(now - due) >= 0;
}


// True when interval has passed since the counter stood at since. The difference is an unsigned count, so the answer is right across a
// wrap for any interval below 2^32. A since that lies ahead of the counter gives a difference of 2^32 minus the distance, which is large,
// so the callers keep since at or before the counter.
constexpr bool Elapsed(uint32 now, uint32 since, uint32 interval) {
	return now - since >= interval;
}


// A 64-bit call takes uint64 for every argument. A call that mixes uint64 with another type matches the deleted forms below, so it does
// not compile.
template<typename... Tick>
concept AnyUint64 = (std::same_as<Tick, uint64> || ...);


template<typename... Tick>
concept AllUint64 = (std::same_as<Tick, uint64> && ...);


// True when the counter has reached due, compared as given. There is no wrap.
template<typename Tick>
requires std::same_as<Tick, uint64>
constexpr bool Reached(Tick now, Tick due) {
	return now >= due;
}


// True when interval has passed since the counter stood at since. There is no wrap: a since after the counter gives false.
template<typename Tick>
requires std::same_as<Tick, uint64>
constexpr bool Elapsed(Tick now, Tick since, Tick interval) {
	return now >= since && now - since >= interval;
}


// A call with one uint64 argument and another argument of a different type. The 32-bit form would narrow the uint64 value, so the call
// is rejected; the caller passes uint64 for both arguments.
template<typename Now, typename Due>
requires (AnyUint64<Now, Due> && !AllUint64<Now, Due>)
bool Reached(Now now, Due due) = delete;


template<typename Now, typename Since, typename Interval>
requires (AnyUint64<Now, Since, Interval> && !AllUint64<Now, Since, Interval>)
bool Elapsed(Now now, Since since, Interval interval) = delete;


// The whole seconds of a span of milliseconds, and the microseconds of the rest.
constexpr Split SplitMs(uint32 ms) {
	return Split{ms / 1000, (ms % 1000) * usPerMs};
}


}
