// expect: SampleSourceOf
#include <Utilities/Math/FFT/RealFFT.h>


// A raw uint16 buffer is not a float sample source. The FFT needs AdcSamples to convert the codes.
void Run(RealFFT<256> &fft, const uint16 (&codes)[256], float (&amplitude)[129]) {
	fft.Analyze(codes, amplitude);
}
