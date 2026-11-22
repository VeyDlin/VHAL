#include <Utilities/Math/FFT/RealFFT.h>
#include <Utilities/Math/IQMath/IQMath.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <type_traits>


// Host tests of RealFFT against a naive DFT in long double. The reference reads back the quantized input each type sees,
// so the error measures the transform only. Forward is compared with spectrumScale times the DFT, divided by Size. Analyze
// is compared with the windowed DFT magnitude times 2 / (Size * coherentGain), which is the amplitude of a sine at its bin.
//
// Cases: DC of 0.5 and of full scale 1, a sine at a bin centre and one between two bins, uniform noise in -1..1, full-scale
// noise of +-1, a unit sine, and silence. Each case runs at Size 256 and 1024 with the Hann and the rectangular window.
// Full scale must not saturate any output, so a saturated raw value is a failure.
//
// Each bound is the worst error measured over these cases for its type, plus a margin of about 2x, rounded up.


namespace {


int failureCount = 0;

constexpr long double pi = 3.14159265358979323846264338327950288L;
constexpr uint64 noiseSeed = 0x9E3779B97F4A7C15ull;


void Fail(int line, const char* what, double value, double bound) {
	failureCount++;
	std::fprintf(stderr, "TestRealFFT.cpp:%d: %s %.6g, bound %.6g\n", line, what, value, bound);
}


// Reads a value of any tested type as a double, exactly: float and double directly, IQ from its raw value.
template <typename T>
double Real(T value) {
	if constexpr (std::is_floating_point_v<T>) {
		return static_cast<double>(value);
	} else {
		return value.ToDouble();
	}
}


// Converts a double to the tested type: float rounds, IQ rounds to nearest.
template <typename T>
T Make(double value) {
	return static_cast<T>(value);
}


// True when an IQ value sits at either end of its range, which is where a saturating operation leaves it.
template <typename T>
bool IsSaturated(T value) {
	if constexpr (std::is_floating_point_v<T>) {
		return false;
	} else {
		return value.raw == IQDetail::maxRaw || value.raw == IQDetail::minRaw;
	}
}


// Per-type error bounds. Each is the worst measured error of its type plus a margin.
// Measured worst: float 2.8e-8, double 1.1e-16, IQ<16> 4.0e-5, IQ<24> 1.4e-7.
template <typename T>
constexpr double ForwardBound() {
	if constexpr (std::is_same_v<T, float>) {
		return 1.0e-7;
	} else if constexpr (std::is_same_v<T, double>) {
		return 1.0e-15;
	} else if constexpr (std::is_same_v<T, iq16>) {
		return 1.0e-4;
	} else {
		return 3.0e-7;
	}
}


// Measured worst: float 6.0e-8, double 6.7e-16, IQ<16> 1.24e-4 (8 LSB), IQ<24> 5.6e-7 (9 LSB).
template <typename T>
constexpr double AnalyzeBound() {
	if constexpr (std::is_same_v<T, float>) {
		return 1.5e-7;
	} else if constexpr (std::is_same_v<T, double>) {
		return 2.0e-15;
	} else if constexpr (std::is_same_v<T, iq16>) {
		return 2.5e-4;
	} else {
		return 1.2e-6;
	}
}


// Fills signal with case caseIndex. The noise draws from a fixed-seed generator, so every run sees the same values.
void MakeSignal(int caseIndex, uint32 size, double* signal) {
	uint64 state = noiseSeed;
	for (uint32 index = 0; index < size; index++) {
		long double angle = 2.0L * pi * static_cast<long double>(index) / static_cast<long double>(size);
		double value = 0.0;
		state = state * 6364136223846793005ull + 1442695040888963407ull;
		double uniform = static_cast<double>(state >> 11) * (1.0 / 9007199254740992.0);
		switch (caseIndex) {
			case 0:
				value = 0.5;
				break;
			case 1:
				value = 1.0;
				break;
			case 2:
				value = 0.8 * static_cast<double>(std::sin(angle * 10.0L));
				break;
			case 3:
				value = 0.8 * static_cast<double>(std::sin(angle * 10.5L));
				break;
			case 4:
				value = 2.0 * uniform - 1.0;
				break;
			case 5:
				value = uniform < 0.5 ? -1.0 : 1.0;
				break;
			case 6:
				value = static_cast<double>(std::sin(angle * 16.0L));
				break;
			default:
				value = 0.0;
				break;
		}
		signal[index] = value;
	}
}


struct Worst {
	double forward = 0.0;
	double analyze = 0.0;
};


template <typename T, uint32 Size, typename Window>
void RunCase(const char* typeName, const char* windowName, int caseIndex, Worst& worst) {
	constexpr bool hann = std::is_same_v<Window, HannWindow>;
	constexpr uint32 bins = Size / 2 + 1;
	using FFT = RealFFT<Size, T, Window>;

	double signal[Size] = {};
	MakeSignal(caseIndex, Size, signal);

	T samples[Size] = {};
	long double quantized[Size] = {};
	for (uint32 index = 0; index < Size; index++) {
		samples[index] = Make<T>(signal[index]);
		quantized[index] = static_cast<long double>(Real(samples[index]));
	}

	// The reference: cos and sin of 2 pi m / Size, the window, and the DFT at every bin.
	long double cosTable[Size] = {};
	long double sinTable[Size] = {};
	for (uint32 index = 0; index < Size; index++) {
		long double angle = 2.0L * pi * static_cast<long double>(index) / static_cast<long double>(Size);
		cosTable[index] = std::cos(angle);
		sinTable[index] = std::sin(angle);
	}

	long double weighted[Size] = {};
	for (uint32 index = 0; index < Size; index++) {
		long double taper = hann ? 0.5L - 0.5L * cosTable[index] : 1.0L;
		weighted[index] = taper * quantized[index];
	}

	Complex<T> spectrum[bins];
	T amplitude[bins] = {};
	FFT fft;
	fft.Forward(samples, spectrum);
	fft.Analyze(samples, amplitude);

	const long double scale = static_cast<long double>(FFT::spectrumScale);
	const long double amplitudeFactor = 2.0L / (static_cast<long double>(Size) * static_cast<long double>(Window::coherentGain));

	double forwardError = 0.0;
	double analyzeError = 0.0;
	bool saturated = false;
	for (uint32 bin = 0; bin < bins; bin++) {
		uint32 phaseRow = 0;
		long double plainRe = 0.0L;
		long double plainIm = 0.0L;
		long double tapedRe = 0.0L;
		long double tapedIm = 0.0L;
		for (uint32 index = 0; index < Size; index++) {
			phaseRow = (bin * index) % Size;
			plainRe += quantized[index] * cosTable[phaseRow];
			plainIm -= quantized[index] * sinTable[phaseRow];
			tapedRe += weighted[index] * cosTable[phaseRow];
			tapedIm -= weighted[index] * sinTable[phaseRow];
		}

		long double gotRe = static_cast<long double>(Real(spectrum[bin].re)) / scale;
		long double gotIm = static_cast<long double>(Real(spectrum[bin].im)) / scale;
		double errorRe = static_cast<double>(std::fabs(gotRe - plainRe) / static_cast<long double>(Size));
		double errorIm = static_cast<double>(std::fabs(gotIm - plainIm) / static_cast<long double>(Size));
		if (errorRe > forwardError) {
			forwardError = errorRe;
		}
		if (errorIm > forwardError) {
			forwardError = errorIm;
		}

		long double referenceAmplitude = std::sqrt(tapedRe * tapedRe + tapedIm * tapedIm) * amplitudeFactor;
		double errorAmplitude = static_cast<double>(std::fabs(static_cast<long double>(Real(amplitude[bin])) - referenceAmplitude));
		if (errorAmplitude > analyzeError) {
			analyzeError = errorAmplitude;
		}

		if (IsSaturated(spectrum[bin].re) || IsSaturated(spectrum[bin].im) || IsSaturated(amplitude[bin])) {
			saturated = true;
		}
	}

	if (forwardError > worst.forward) {
		worst.forward = forwardError;
	}
	if (analyzeError > worst.analyze) {
		worst.analyze = analyzeError;
	}

	const char* flag = saturated ? "  SATURATED" : "";
	std::printf("%-8s %5u %-11s case %d  forward %.3e  analyze %.3e%s\n",
		typeName,
		static_cast<unsigned>(Size),
		windowName,
		caseIndex,
		forwardError,
		analyzeError,
		flag
	);

	if (forwardError > ForwardBound<T>()) {
		Fail(__LINE__, "Forward error", forwardError, ForwardBound<T>());
	}
	if (analyzeError > AnalyzeBound<T>()) {
		Fail(__LINE__, "Analyze error", analyzeError, AnalyzeBound<T>());
	}
	if (saturated) {
		Fail(__LINE__, "saturated output in case", static_cast<double>(caseIndex), 0.0);
	}
}


template <typename T, uint32 Size, typename Window>
void RunAllCases(const char* typeName, const char* windowName) {
	Worst worst;
	for (int caseIndex = 0; caseIndex <= 7; caseIndex++) {
		RunCase<T, Size, Window>(typeName, windowName, caseIndex, worst);
	}
	std::printf("worst %-8s %5u %-11s forward %.3e  analyze %.3e\n", typeName, static_cast<unsigned>(Size), windowName, worst.forward, worst.analyze);
}


template <typename T, uint32 Size>
void RunType(const char* typeName) {
	RunAllCases<T, Size, HannWindow>(typeName, "hann");
	RunAllCases<T, Size, RectangularWindow>(typeName, "rectangular");
}


template <typename T>
void RunSizes(const char* typeName) {
	RunType<T, 256>(typeName);
	RunType<T, 1024>(typeName);
}


// The scale constants: Forward's spectrum is the DFT times spectrumScale, and Analyze's factor is 2 / (Size * gain) over that.
template <typename T, uint32 Size, typename Window>
void CheckScales(const char* typeName, double expectedSpectrumScale, double expectedAmplitudeScale) {
	using FFT = RealFFT<Size, T, Window>;
	if (FFT::spectrumScale != expectedSpectrumScale) {
		Fail(__LINE__, "spectrumScale", FFT::spectrumScale, expectedSpectrumScale);
	}
	if (FFT::amplitudeScale != expectedAmplitudeScale) {
		Fail(__LINE__, "amplitudeScale", FFT::amplitudeScale, expectedAmplitudeScale);
	}
	std::printf("scales %-8s %5u spectrumScale %.6g amplitudeScale %.6g\n", typeName, static_cast<unsigned>(Size), FFT::spectrumScale, FFT::amplitudeScale);
}


// A unit sine at bin 16 has amplitude 1 at bin 16 under either window. The rectangular window leaves every other bin at
// zero. The Hann window leaves 0.5 at the two neighbours, bins 15 and 17, and zero beyond them.
template <typename T, uint32 Size, typename Window>
void CheckUnitSine(const char* typeName, const char* windowName) {
	using FFT = RealFFT<Size, T, Window>;
	constexpr uint32 bins = Size / 2 + 1;
	constexpr bool hann = std::is_same_v<Window, HannWindow>;
	double signal[Size] = {};
	MakeSignal(6, Size, signal);
	T samples[Size] = {};
	for (uint32 index = 0; index < Size; index++) {
		samples[index] = Make<T>(signal[index]);
	}
	T amplitude[bins] = {};
	FFT fft;
	fft.Analyze(samples, amplitude);

	double peakError = std::fabs(Real(amplitude[16]) - 1.0);
	if (peakError > AnalyzeBound<T>()) {
		Fail(__LINE__, "unit sine peak error", peakError, AnalyzeBound<T>());
	}
	double neighbourError = 0.0;
	double farLeak = 0.0;
	for (uint32 bin = 0; bin < bins; bin++) {
		if (bin == 16) {
			continue;
		}
		bool neighbour = bin == 15 || bin == 17;
		if (neighbour && hann) {
			double error = std::fabs(Real(amplitude[bin]) - 0.5);
			if (error > neighbourError) {
				neighbourError = error;
			}
		} else if (Real(amplitude[bin]) > farLeak) {
			farLeak = Real(amplitude[bin]);
		}
	}
	if (neighbourError > AnalyzeBound<T>()) {
		Fail(__LINE__, "Hann neighbour error", neighbourError, AnalyzeBound<T>());
	}
	if (farLeak > AnalyzeBound<T>()) {
		Fail(__LINE__, "unit sine leakage", farLeak, AnalyzeBound<T>());
	}
	std::printf("unit sine %-8s %5u %-11s peak error %.3e  neighbours %.3e  leakage %.3e\n",
		typeName,
		static_cast<unsigned>(Size),
		windowName,
		peakError,
		neighbourError,
		farLeak
	);
}


// The compile-time tables against long double over the whole circle: the float twiddles and the Hann window, and the IQ
// twiddles, which must be the nearest Q30 value. Exact quarter turns must be exact.
template <uint32 Size>
void CheckTables() {
	constexpr uint32 halfSize = Size / 2;
	const FFTDetail::Tables<Size, double> &doubleTables = FFTDetail::fftTables<Size, double>;
	const FFTDetail::Tables<Size, iq24> &integerTables = FFTDetail::fftTables<Size, iq24>;
	const double *window = HannWindow::table<Size, double>.values;

	double worstTwiddle = 0.0;
	double worstWindow = 0.0;
	double worstInteger = 0.0;
	for (uint32 index = 0; index < halfSize; index++) {
		long double angle = 2.0L * pi * static_cast<long double>(index) / static_cast<long double>(Size);
		double cosineError = std::fabs(doubleTables.re[index] - static_cast<double>(std::cos(angle)));
		double sineError = std::fabs(doubleTables.im[index] + static_cast<double>(std::sin(angle)));
		if (cosineError > worstTwiddle) {
			worstTwiddle = cosineError;
		}
		if (sineError > worstTwiddle) {
			worstTwiddle = sineError;
		}

		long double windowValue = 0.5L - 0.5L * std::cos(angle);
		double windowError = std::fabs(window[index] - static_cast<double>(windowValue));
		if (windowError > worstWindow) {
			worstWindow = windowError;
		}

		double scaledCosine = static_cast<double>(std::cos(angle)) * 1073741824.0;
		double scaledSine = -static_cast<double>(std::sin(angle)) * 1073741824.0;
		double integerError = std::fabs(static_cast<double>(integerTables.re[index]) - scaledCosine);
		if (integerError > worstInteger) {
			worstInteger = integerError;
		}
		integerError = std::fabs(static_cast<double>(integerTables.im[index]) - scaledSine);
		if (integerError > worstInteger) {
			worstInteger = integerError;
		}
	}

	std::printf("tables %5u twiddle %.3e  window %.3e  iq twiddle %.3f LSB of Q30\n",
		static_cast<unsigned>(Size),
		worstTwiddle,
		worstWindow,
		worstInteger
	);
	if (worstTwiddle > 1.0e-15) {
		Fail(__LINE__, "twiddle error", worstTwiddle, 1.0e-15);
	}
	if (worstWindow > 1.0e-15) {
		Fail(__LINE__, "window error", worstWindow, 1.0e-15);
	}
	if (worstInteger > 0.5000001) {
		Fail(__LINE__, "IQ twiddle error in LSB", worstInteger, 0.5000001);
	}

	if (doubleTables.re[0] != 1.0 || doubleTables.im[0] != 0.0) {
		Fail(__LINE__, "twiddle 0 is not 1", doubleTables.re[0], 1.0);
	}
	if (doubleTables.re[Size / 4] != 0.0 || doubleTables.im[Size / 4] != -1.0) {
		Fail(__LINE__, "quarter-turn twiddle is not exact", doubleTables.re[Size / 4], 0.0);
	}
	if (window[0] != 0.0 || window[Size / 2] != 1.0) {
		Fail(__LINE__, "Hann window ends are not exact", window[Size / 2], 1.0);
	}
}


// Silence gives exact zeros for every type.
template <typename T, uint32 Size>
void CheckSilence(const char* typeName) {
	RealFFT<Size, T> fft;
	T samples[Size] = {};
	T amplitude[Size / 2 + 1] = {};
	fft.Analyze(samples, amplitude);
	for (uint32 bin = 0; bin <= Size / 2; bin++) {
		if (Real(amplitude[bin]) != 0.0) {
			Fail(__LINE__, "silence is not zero", Real(amplitude[bin]), 0.0);
		}
	}
	std::printf("silence %-8s %5u all zero\n", typeName, static_cast<unsigned>(Size));
}


}


int main() {
	CheckTables<256>();
	CheckTables<1024>();

	RunSizes<float>("float");
	RunSizes<double>("double");
	RunSizes<iq16>("iq16");
	RunSizes<iq24>("iq24");

	CheckScales<float, 256, HannWindow>("float", 1.0, 4.0 / 256.0);
	CheckScales<float, 1024, RectangularWindow>("float", 1.0, 2.0 / 1024.0);
	CheckScales<double, 256, HannWindow>("double", 1.0, 4.0 / 256.0);
	CheckScales<iq16, 256, HannWindow>("iq16", 2.0 / 256.0, 2.0);
	CheckScales<iq24, 1024, RectangularWindow>("iq24", 2.0 / 1024.0, 1.0);

	CheckUnitSine<float, 256, HannWindow>("float", "hann");
	CheckUnitSine<float, 256, RectangularWindow>("float", "rectangular");
	CheckUnitSine<double, 1024, HannWindow>("double", "hann");
	CheckUnitSine<double, 1024, RectangularWindow>("double", "rectangular");
	CheckUnitSine<iq16, 256, HannWindow>("iq16", "hann");
	CheckUnitSine<iq16, 256, RectangularWindow>("iq16", "rectangular");
	CheckUnitSine<iq24, 1024, HannWindow>("iq24", "hann");
	CheckUnitSine<iq24, 1024, RectangularWindow>("iq24", "rectangular");

	CheckSilence<float, 256>("float");
	CheckSilence<iq24, 1024>("iq24");

	if (failureCount != 0) {
		std::fprintf(stderr, "TestRealFFT: %d failure(s)\n", failureCount);
		return 1;
	}
	std::printf("TestRealFFT: all checks passed\n");
	return 0;
}
