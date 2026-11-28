// expect: use of deleted function
#include <Utilities/Time/Ticks.h>


constexpr uint64 totalUs = 5000000;

constexpr bool reached = Ticks::Reached(totalUs, 4000000);
