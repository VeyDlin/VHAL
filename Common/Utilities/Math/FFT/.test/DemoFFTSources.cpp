#include <Utilities/Math/FFT/AdcSamples.h>
#include <Utilities/Math/FFT/FrameCollector.h>
#include <Utilities/Math/FFT/LatestSamples.h>
#include <Utilities/Math/FFT/RealFFT.h>
#include <Utilities/Math/IQMath/IQMath.h>
#include <cmath>
#include <cstdio>
#include <type_traits>


// A host demo: one tone through each sample source and each number type. The tone is a sine at bin 40 of a 256-point Hann
// transform, amplitude 0.5. The ADC source gets the same tone as 12-bit codes, centred on 2048, so its error includes the code
// quantization. For each line the demo prints the peak bin, its amplitude and the error from 0.5.


namespace {


constexpr uint32 size = 256;
constexpr uint32 toneBin = 40;
constexpr double toneAmplitude = 0.5;
constexpr long double pi = 3.14159265358979323846264338327950288L;


template <typename T>
double AsDouble(T value) {
	if constexpr (std::is_floating_point_v<T>) {
		return static_cast<double>(value);
	} else {
		return static_cast<double>(value.raw) / static_cast<double>(value.one);
	}
}


void MakeTone(double *tone) {
	for (uint32 index = 0; index < size; index++) {
		long double angle = 2.0L * pi * static_cast<long double>(toneBin) * static_cast<long double>(index) / static_cast<long double>(size);
		tone[index] = toneAmplitude * static_cast<double>(std::sin(angle));
	}
}


// Prints the peak bin of an amplitude spectrum, its amplitude and its error from the tone amplitude.
template <typename T>
void PrintPeak(const char *numberName, const char *sourceName, const T *amplitude) {
	constexpr uint32 bins = size / 2 + 1;
	uint32 peak = 0;
	for (uint32 bin = 1; bin < bins; bin++) {
		if (AsDouble(amplitude[bin]) > AsDouble(amplitude[peak])) {
			peak = bin;
		}
	}
	double value = AsDouble(amplitude[peak]);
	std::printf("%-6s %-10s peak bin %3u  amplitude %.6f  error %+.3e\n",
		numberName,
		sourceName,
		static_cast<unsigned>(peak),
		value,
		value - toneAmplitude
	);
}


template <typename T>
void Report(const char *numberName) {
	constexpr uint32 bins = size / 2 + 1;
	double tone[size] = {};
	MakeTone(tone);

	RealFFT<size, T> fft;
	T amplitude[bins] = {};

	T samples[size] = {};
	for (uint32 index = 0; index < size; index++) {
		if constexpr (IQType<T>) {
			samples[index] = RealFrom<T>(static_cast<float>(tone[index]));
		} else {
			samples[index] = static_cast<T>(tone[index]);
		}
	}
	fft.Analyze(samples, amplitude);
	PrintPeak(numberName, "array", amplitude);

	uint16 codes[size] = {};
	for (uint32 index = 0; index < size; index++) {
		codes[index] = static_cast<uint16>(2048 + std::lround(tone[index] * 2048.0));
	}
	AdcSamples<T, 12> adc(codes);
	fft.Analyze(adc, amplitude);
	PrintPeak(numberName, "adc12", amplitude);

	LatestSamples<T, size> latest;
	for (uint32 index = 0; index < size; index++) {
		latest.Push(samples[index]);
	}
	fft.Analyze(latest, amplitude);
	PrintPeak(numberName, "latest", amplitude);

	FrameCollector<T, size> frames;
	for (uint32 index = 0; index < size; index++) {
		frames.Push(samples[index]);
	}
	{
		typename FrameCollector<T, size>::Frame frame = frames.Latest();
		fft.Analyze(frame, amplitude);
	}
	PrintPeak(numberName, "frame", amplitude);
}


}


int main() {
	std::printf("tone at bin %u, amplitude %.2f, Hann window, %u samples\n", static_cast<unsigned>(toneBin), toneAmplitude, static_cast<unsigned>(size));
	Report<float>("float");
	Report<iq16>("iq16");
	Report<iq24>("iq24");
	return 0;
}
