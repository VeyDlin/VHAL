// expect: AdcSamples: Bits must be in 2..16
#include <Utilities/Math/FFT/AdcSamples.h>
#include <Utilities/Math/IQMath/IQMath.h>


// The codes are uint16, so a resolution above 16 bits cannot be read from them.
AdcSamples<iq24, 20> samples(nullptr);
