// expect: RealFFT: IQ needs Q <= 29
#include <Utilities/Math/FFT/RealFFT.h>
#include <Utilities/Math/IQMath/IQ.h>


// A spectrum bin of a full-scale input can reach 2, and IQ<30> holds only values below 2, so the spectrum would saturate.
constexpr uint32 tooWide = RealFFT<256, IQ<30>>::size;
