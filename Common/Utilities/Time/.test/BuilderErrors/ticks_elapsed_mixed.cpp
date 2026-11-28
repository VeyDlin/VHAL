// expect: use of deleted function
#include <Utilities/Time/Ticks.h>


constexpr uint64 totalUs = 5000000;

constexpr bool elapsed = Ticks::Elapsed(totalUs, uint64{4000000}, 1000000);
