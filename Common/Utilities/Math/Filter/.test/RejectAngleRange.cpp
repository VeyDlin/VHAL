#include <Utilities/Math/Filter/ComplementaryFilter.h>
#include <Utilities/Math/IQMath/IQMath.h>


// Must not compile. IQ<24> has range 128 and cannot hold the 180 degree angle. RejectAngleRange.cmake expects the compiler to reject this
// file with a RangeAtLeast message.
ComplementaryFilter<IQ<24>> rejected;
