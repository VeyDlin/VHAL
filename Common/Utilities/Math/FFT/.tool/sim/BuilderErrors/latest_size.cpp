// expect: LatestSamples: Size must be a power of two
#include <Utilities/Math/FFT/LatestSamples.h>


// The ring index is a mask, so the size must be a power of two.
LatestSamples<float, 100> latest;
