#include <Utilities/Math/FFT/RealFFT.h>
#include <VhalCheck.h>
#include <cmath>
#include <cstdint>
#include <vector>


// Host tests of RealFFT<512> (float) against a direct DFT in double precision: the Nyquist signal, the scale of a windowed tone at
// its bin, the two-tone resolution, the scalloping loss halfway between two bins, Parseval's relation, and the smallest size, N = 8.
// The signals are built in double and rounded to float, so a wrong bit order, twiddle or unpacking shows up in the first check
// that touches it.


namespace {


const double pi = 3.14159265358979323846;
const uint32 n = 512;
const double sampleRateHz = 16000.0;
using Fft = RealFFT<512>;


// Deterministic noise in -1..1 (a 64-bit LCG), so every run checks the same signal.
std::vector<float> Noise(uint32 count, uint64 seed) {
	std::vector<float> values(count);
	uint64 state = seed;
	for (uint32 index = 0; index < count; index++) {
		state = state * 6364136223846793005ULL + 1442695040888963407ULL;
		values[index] = static_cast<float>(static_cast<double>(state >> 11) / 4503599627370496.0 - 1.0);
	}

	return values;
}


std::vector<float> Tone(double frequencyHz, double amplitude, uint32 count) {
	std::vector<float> values(count);
	for (uint32 index = 0; index < count; index++) {
		values[index] = static_cast<float>(amplitude * std::sin(2.0 * pi * frequencyHz * index / sampleRateHz));
	}

	return values;
}


// Direct DFT in double precision, the reference of the fast transform.
void NaiveDft(const std::vector<float>& signal, std::vector<double>& re, std::vector<double>& im) {
	uint32 count = static_cast<uint32>(signal.size());
	re.assign(count / 2 + 1, 0.0);
	im.assign(count / 2 + 1, 0.0);
	for (uint32 bin = 0; bin <= count / 2; bin++) {
		for (uint32 index = 0; index < count; index++) {
			double angle = -2.0 * pi * static_cast<double>(bin) * static_cast<double>(index) / static_cast<double>(count);
			re[bin] += signal[index] * std::cos(angle);
			im[bin] += signal[index] * std::sin(angle);
		}
	}
}


uint32 PeakBin(const float* amplitude, uint32 count) {
	uint32 peak = 0;
	for (uint32 index = 1; index < count; index++) {
		if (amplitude[index] > amplitude[peak]) {
			peak = index;
		}
	}

	return peak;
}


// (-1)^n puts all its energy in the Nyquist bin, which the unpacking takes from the packed DC term.
void CheckNyquistSignal() {
	Fft fft;
	std::vector<float> signal(n);
	for (uint32 index = 0; index < n; index++) {
		signal[index] = (index % 2 == 0) ? 1.0f : -1.0f;
	}

	std::vector<Complex<float>> spectrum(Fft::binCount);
	fft.Forward(signal.data(), spectrum.data());
	VHAL_CHECK_NEAR(spectrum[Fft::binCount - 1].re, 512.0, 1e-3);
	VHAL_CHECK_NEAR(spectrum[Fft::binCount - 1].im, 0.0, 1e-3);
	double largest = 0.0;
	for (uint32 bin = 0; bin < Fft::binCount - 1; bin++) {
		double magnitude = std::hypot(spectrum[bin].re, spectrum[bin].im);
		if (magnitude > largest) {
			largest = magnitude;
		}
	}

	VHAL_CHECK_NEAR(largest, 0.0, 1e-3);
}


// 1000 Hz is bin 32 exactly, so the Hann-windowed amplitude reads the true 0.5.
void CheckWindowedToneAmplitude() {
	Fft fft;
	std::vector<float> signal = Tone(1000.0, 0.5, n);
	std::vector<float> amplitude(Fft::binCount);
	fft.Analyze(signal.data(), amplitude.data());
	VHAL_CHECK(PeakBin(amplitude.data(), Fft::binCount) == 32);
	// The periodic Hann window leaks half the amplitude into the two neighbouring bins and nothing further out.
	VHAL_CHECK_NEAR(amplitude[32], 0.5, 0.005);
	VHAL_CHECK_NEAR(amplitude[31], 0.25, 0.005);
	VHAL_CHECK_NEAR(amplitude[33], 0.25, 0.005);
	VHAL_CHECK_NEAR(amplitude[30], 0.0, 0.005);
}


// 1000 Hz at 0.5 (bin 32) and 3000 Hz at 0.25 (bin 96): two peaks with their own amplitudes, and nothing between.
void CheckTwoTonesResolve() {
	Fft fft;
	std::vector<float> first = Tone(1000.0, 0.5, n);
	std::vector<float> second = Tone(3000.0, 0.25, n);
	std::vector<float> signal(n);
	for (uint32 index = 0; index < n; index++) {
		signal[index] = first[index] + second[index];
	}

	std::vector<float> amplitude(Fft::binCount);
	fft.Analyze(signal.data(), amplitude.data());
	VHAL_CHECK_NEAR(amplitude[32], 0.5, 0.005);
	VHAL_CHECK_NEAR(amplitude[96], 0.25, 0.005);
	VHAL_CHECK_NEAR(amplitude[64], 0.0, 0.01);
	VHAL_CHECK_NEAR(amplitude[128], 0.0, 0.01);
}


// Halfway between bins 32 and 33 the Hann peak is about 0.85 of the amplitude (1.42 dB of scalloping loss).
void CheckHalfBinScalloping() {
	Fft fft;
	double frequencyHz = 32.5 * sampleRateHz / n;
	std::vector<float> signal = Tone(frequencyHz, 1.0, n);
	std::vector<float> amplitude(Fft::binCount);
	fft.Analyze(signal.data(), amplitude.data());
	double peak = amplitude[32] > amplitude[33] ? amplitude[32] : amplitude[33];
	VHAL_CHECK(peak > 0.83 && peak < 0.87);
}


// Sum of squares in time equals (|X0|^2 + |X[N/2]|^2 + 2 sum |Xk|^2) / N over the full spectrum.
void CheckParseval() {
	Fft fft;
	std::vector<float> signal = Noise(n, 987654321);
	std::vector<Complex<float>> spectrum(Fft::binCount);
	fft.Forward(signal.data(), spectrum.data());
	double timeEnergy = 0.0;
	for (uint32 index = 0; index < n; index++) {
		timeEnergy += static_cast<double>(signal[index]) * signal[index];
	}

	double frequencyEnergy = 0.0;
	for (uint32 bin = 0; bin < Fft::binCount; bin++) {
		double power = static_cast<double>(spectrum[bin].re) * spectrum[bin].re
			+ static_cast<double>(spectrum[bin].im) * spectrum[bin].im;
		bool edge = bin == 0 || bin == Fft::binCount - 1;
		frequencyEnergy += edge ? power : 2.0 * power;
	}

	frequencyEnergy = frequencyEnergy / n;
	VHAL_CHECK_NEAR(frequencyEnergy / timeEnergy, 1.0, 1e-5);
}


// The same code at the smallest useful size: N = 8 with a non-symmetric input.
void CheckSmallSizeMatchesNaiveDft() {
	RealFFT<8> small;
	std::vector<float> signal = { 0.5f, -1.0f, 0.25f, 2.0f, -0.75f, 0.0f, 1.5f, -0.125f };
	std::vector<Complex<float>> spectrum(RealFFT<8>::binCount);
	small.Forward(signal.data(), spectrum.data());
	std::vector<double> referenceRe;
	std::vector<double> referenceIm;
	NaiveDft(signal, referenceRe, referenceIm);
	double worst = 0.0;
	for (uint32 bin = 0; bin < RealFFT<8>::binCount; bin++) {
		double errorRe = std::fabs(spectrum[bin].re - referenceRe[bin]);
		double errorIm = std::fabs(spectrum[bin].im - referenceIm[bin]);
		if (errorRe > worst) {
			worst = errorRe;
		}

		if (errorIm > worst) {
			worst = errorIm;
		}
	}

	VHAL_CHECK_NEAR(worst, 0.0, 1e-5);
}


}


int main() {
	CheckNyquistSignal();
	CheckWindowedToneAmplitude();
	CheckTwoTonesResolve();
	CheckHalfBinScalloping();
	CheckParseval();
	CheckSmallSizeMatchesNaiveDft();
	return VhalCheck::Finish("TestRealFFTReference");
}
