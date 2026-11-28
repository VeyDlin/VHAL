// expect: use of deleted function
#include <Utilities/Time/Ticks.h>


constexpr uint64 totalUs = 5000000;
constexpr uint32 dueTick = 4000000u;

constexpr bool reached = Ticks::Reached(totalUs, dueTick);
