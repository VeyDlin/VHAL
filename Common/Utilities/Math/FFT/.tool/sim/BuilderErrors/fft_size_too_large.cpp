// expect: RealFFT: Size must be at most 2^17
#include <Utilities/Math/FFT/RealFFT.h>


// The bit-reversal table holds 16-bit indices, so a larger transform would silently wrap them.
constexpr uint32 tooLarge = RealFFT<262144>::size;
