// expect: SampleSourceOf
#include <Utilities/Math/FFT/AdcSamples.h>
#include <Utilities/Math/FFT/RealFFT.h>
#include <Utilities/Math/IQMath/IQMath.h>


// A float view cannot feed an IQ transform: the source must give the transform's Number exactly.
void Run(RealFFT<256, iq24> &fft, AdcSamples<float, 12> &samples, iq24 (&amplitude)[129]) {
	fft.Analyze(samples, amplitude);
}
