#include <Utilities/Math/FFT/AdcSamples.h>
#include <Utilities/Math/FFT/FrameCollector.h>
#include <Utilities/Math/FFT/LatestSamples.h>
#include <Utilities/Math/FFT/RealFFT.h>
#include <Utilities/Math/IQMath/IQMath.h>
#include <cmath>
#include <cstdio>
#include <type_traits>


// Host tests of the sample sources. Each source runs through Analyze and Forward, and must give exactly the spectrum of a plain
// array holding the same samples. AdcSamples is also checked against hand-computed conversions at the ends of the code range
// and at the rounding ties. LatestSamples is checked for order, zero-fill and wrap-around; FrameCollector for its hold, its
// dropped frame and its hop. The threaded checks are in TestSourceThreads.cpp.


namespace {


int failureCount = 0;

constexpr long double pi = 3.14159265358979323846264338327950288L;


void Fail(int line, const char *what, double value, double expected) {
	failureCount++;
	std::fprintf(stderr, "TestSampleSources.cpp:%d: %s %.9g, expected %.9g\n", line, what, value, expected);
}


// A tested value as a double: float and double directly, IQ from its raw value.
template <typename T>
double AsDouble(T value) {
	if constexpr (std::is_floating_point_v<T>) {
		return static_cast<double>(value);
	} else {
		return static_cast<double>(value.raw) / static_cast<double>(value.one);
	}
}


// A bin-centre sine, a sine between two bins and a DC term, all within -0.7..0.7.
template <typename T, uint32 Size>
void MakeSignal(T *signal) {
	for (uint32 index = 0; index < Size; index++) {
		long double angle = 2.0L * pi * static_cast<long double>(index) / static_cast<long double>(Size);
		double value = 0.3 * static_cast<double>(std::sin(angle * 5.0L))
			+ 0.2 * static_cast<double>(std::sin(angle * 21.5L))
			+ 0.1;
		if constexpr (IQType<T>) {
			signal[index] = RealFrom<T>(static_cast<float>(value));
		} else {
			signal[index] = static_cast<T>(value);
		}
	}
}


template <typename T, uint32 Bins>
bool SameAmplitudes(const T *left, const T *right) {
	for (uint32 bin = 0; bin < Bins; bin++) {
		if (!(left[bin] == right[bin])) {
			return false;
		}
	}
	return true;
}


template <typename T, uint32 Bins>
bool SameSpectrum(const Complex<T> *left, const Complex<T> *right) {
	for (uint32 bin = 0; bin < Bins; bin++) {
		if (!(left[bin].re == right[bin].re) || !(left[bin].im == right[bin].im)) {
			return false;
		}
	}
	return true;
}


// Every source gives the same Analyze and Forward results as the plain array.
template <typename T, uint32 Size>
void CheckSourcesAgree(const char *typeName) {
	constexpr uint32 bins = Size / 2 + 1;
	T signal[Size] = {};
	MakeSignal<T, Size>(signal);

	RealFFT<Size, T> fft;
	T plainAmplitude[bins] = {};
	Complex<T> plainSpectrum[bins];
	fft.Analyze(signal, plainAmplitude);
	fft.Forward(signal, plainSpectrum);

	const T *pointer = signal;
	T pointerAmplitude[bins] = {};
	fft.Analyze(pointer, pointerAmplitude);
	if (!SameAmplitudes<T, bins>(plainAmplitude, pointerAmplitude)) {
		Fail(__LINE__, "pointer source differs from the array", 1.0, 0.0);
	}

	LatestSamples<T, Size> latest;
	for (uint32 index = 0; index < Size; index++) {
		latest.Push(signal[index]);
	}
	T latestAmplitude[bins] = {};
	Complex<T> latestSpectrum[bins];
	fft.Analyze(latest, latestAmplitude);
	fft.Forward(latest, latestSpectrum);
	if (!SameAmplitudes<T, bins>(plainAmplitude, latestAmplitude)) {
		Fail(__LINE__, "LatestSamples Analyze differs from the array", 1.0, 0.0);
	}
	if (!SameSpectrum<T, bins>(plainSpectrum, latestSpectrum)) {
		Fail(__LINE__, "LatestSamples Forward differs from the array", 1.0, 0.0);
	}

	FrameCollector<T, Size> frames;
	for (uint32 index = 0; index < Size; index++) {
		frames.Push(signal[index]);
	}
	typename FrameCollector<T, Size>::Frame frame = frames.Latest();
	if (!frame.Valid()) {
		Fail(__LINE__, "frame is not valid after Size samples", 0.0, 1.0);
	}
	T frameAmplitude[bins] = {};
	Complex<T> frameSpectrum[bins];
	fft.Analyze(frame, frameAmplitude);
	fft.Forward(frame, frameSpectrum);
	if (!SameAmplitudes<T, bins>(plainAmplitude, frameAmplitude)) {
		Fail(__LINE__, "FrameCollector Analyze differs from the array", 1.0, 0.0);
	}
	if (!SameSpectrum<T, bins>(plainSpectrum, frameSpectrum)) {
		Fail(__LINE__, "FrameCollector Forward differs from the array", 1.0, 0.0);
	}

	std::printf("sources agree %-8s %5u  Analyze and Forward identical for array, pointer, LatestSamples and frame\n",
		typeName,
		static_cast<unsigned>(Size)
	);
}


// The same codes through AdcSamples and through a plain array converted by hand. Code 0, the mid code and the top code are in
// the set, so the ends of the range are covered.
template <typename T, uint32 Size>
void CheckAdcAgrees(const char *typeName) {
	constexpr uint32 bins = Size / 2 + 1;
	constexpr int32 halfCodes = 2048;
	uint16 codes[Size] = {};
	for (uint32 index = 0; index < Size; index++) {
		codes[index] = static_cast<uint16>((index * 37 + 11) & 4095);
	}
	codes[0] = 0;
	codes[1] = 4095;
	codes[2] = 2048;

	T converted[Size] = {};
	for (uint32 index = 0; index < Size; index++) {
		int32 offset = static_cast<int32>(codes[index]) - halfCodes;
		if constexpr (std::is_floating_point_v<T>) {
			converted[index] = static_cast<T>(static_cast<double>(offset) / 2048.0);
		} else {
			converted[index] = T::FromRaw(static_cast<int32>(static_cast<int64>(offset) * (int64(1) << (T::q - 11))));
		}
	}

	AdcSamples<T, 12> adc(codes);
	RealFFT<Size, T> fft;
	T plainAmplitude[bins] = {};
	T adcAmplitude[bins] = {};
	fft.Analyze(converted, plainAmplitude);
	fft.Analyze(adc, adcAmplitude);
	if (!SameAmplitudes<T, bins>(plainAmplitude, adcAmplitude)) {
		Fail(__LINE__, "AdcSamples Analyze differs from the converted array", 1.0, 0.0);
	}
	std::printf("adc agrees %-8s %5u  Analyze identical to the hand-converted array\n", typeName, static_cast<unsigned>(Size));
}


// Hand-computed conversions. A 12-bit code is offset by 2048 and scaled so that 2048 codes make 1.0.
void CheckAdcEnds() {
	const uint16 codes[3] = { 0, 4095, 2048 };

	AdcSamples<float, 12> adcFloat(codes);
	if (adcFloat[0] != -1.0f) {
		Fail(__LINE__, "float code 0", AsDouble(adcFloat[0]), -1.0);
	}
	if (adcFloat[1] != 2047.0f / 2048.0f) {
		Fail(__LINE__, "float code 4095", AsDouble(adcFloat[1]), 2047.0 / 2048.0);
	}
	if (adcFloat[2] != 0.0f) {
		Fail(__LINE__, "float mid code", AsDouble(adcFloat[2]), 0.0);
	}

	AdcSamples<double, 12> adcDouble(codes);
	if (adcDouble[0] != -1.0 || adcDouble[1] != 2047.0 / 2048.0 || adcDouble[2] != 0.0) {
		Fail(__LINE__, "double ends", AsDouble(adcDouble[0]), -1.0);
	}

	AdcSamples<iq24, 12> adc24(codes);
	if (adc24[0].raw != -16777216) {
		Fail(__LINE__, "iq24 code 0 raw", adc24[0].raw, -16777216.0);
	}
	if (adc24[1].raw != 16769024) {
		Fail(__LINE__, "iq24 code 4095 raw", adc24[1].raw, 16769024.0);
	}
	if (adc24[2].raw != 0) {
		Fail(__LINE__, "iq24 mid code raw", adc24[2].raw, 0.0);
	}

	AdcSamples<iq16, 12> adc16(codes);
	if (adc16[0].raw != -65536 || adc16[1].raw != 65504 || adc16[2].raw != 0) {
		Fail(__LINE__, "iq16 ends raw", adc16[0].raw, -65536.0);
	}

	// IQ<8>: each code is 1/8 of a raw step of the offset, rounded to nearest, ties toward +infinity.
	const uint16 ties[7] = { 2052, 2044, 2043, 2053, 2051, 2050, 0 };
	const int32 expected[7] = { 1, 0, -1, 1, 0, 0, -256 };
	AdcSamples<iq8, 12> adc8(ties);
	for (uint32 index = 0; index < 7; index++) {
		if (adc8[index].raw != expected[index]) {
			Fail(__LINE__, "iq8 rounding raw", adc8[index].raw, expected[index]);
		}
	}
	std::printf("adc ends: float -1, 2047/2048, 0; iq24 raw -16777216, 16769024, 0; iq8 ties rounded as expected\n");
}


// LatestSamples: zero before the first pushes, then the newest Size samples in order, through several wraps.
void CheckLatestWindow() {
	LatestSamples<float, 4> latest;
	float window[4] = {};
	for (uint32 index = 0; index < 4; index++) {
		window[index] = latest[index];
		if (window[index] != 0.0f) {
			Fail(__LINE__, "empty LatestSamples is not zero", window[index], 0.0);
		}
	}

	const float firstPushes[2] = { 1.0f, 2.0f };
	latest.Push(firstPushes[0]);
	latest.Push(firstPushes[1]);
	const float expectedTwo[4] = { 0.0f, 0.0f, 1.0f, 2.0f };
	for (uint32 index = 0; index < 4; index++) {
		if (latest[index] != expectedTwo[index]) {
			Fail(__LINE__, "LatestSamples after two pushes", latest[index], expectedTwo[index]);
		}
	}

	for (float value = 3.0f; value <= 4.0f; value = value + 1.0f) {
		latest.Push(value);
	}
	for (uint32 index = 0; index < 4; index++) {
		if (latest[index] != static_cast<float>(index + 1)) {
			Fail(__LINE__, "LatestSamples after four pushes", latest[index], static_cast<double>(index + 1));
		}
	}

	for (float value = 5.0f; value <= 9.0f; value = value + 1.0f) {
		latest.Push(value);
	}
	for (uint32 index = 0; index < 4; index++) {
		if (latest[index] != static_cast<float>(index + 6)) {
			Fail(__LINE__, "LatestSamples after nine pushes", latest[index], static_cast<double>(index + 6));
		}
	}

	// Sixteen samples, a thousand pushes: the window is the last sixteen values in order.
	LatestSamples<float, 16> long16;
	for (uint32 value = 1; value <= 1000; value++) {
		long16.Push(static_cast<float>(value));
	}
	for (uint32 index = 0; index < 16; index++) {
		if (long16[index] != static_cast<float>(985 + index)) {
			Fail(__LINE__, "LatestSamples after 1000 pushes", long16[index], static_cast<double>(985 + index));
		}
	}

	// Integer samples keep their raw values through the ring.
	LatestSamples<iq24, 4> integer;
	integer.Push(iq24::FromRaw(-5));
	integer.Push(iq24::FromRaw(7));
	if (integer[2].raw != -5 || integer[3].raw != 7 || integer[0].raw != 0) {
		Fail(__LINE__, "LatestSamples iq24 raw", integer[3].raw, 7.0);
	}
	std::printf("LatestSamples window: zero-fill, order, wrap at 4 and 16 over 1000 pushes\n");
}


// FrameCollector with Hop equal to Size: a frame is held while the next one fills, is dropped when the writer needs the held
// buffer, and the frames the reader gets are whole.
void CheckFrameHold() {
	FrameCollector<float, 4> frames;
	{
		FrameCollector<float, 4>::Frame empty = frames.Latest();
		if (empty.Valid() || empty[0] != 0.0f) {
			Fail(__LINE__, "frame before any sample", empty[0], 0.0);
		}
	}

	for (float value = 1.0f; value <= 4.0f; value = value + 1.0f) {
		frames.Push(value);
	}
	{
		FrameCollector<float, 4>::Frame first = frames.Latest();
		if (!first.Valid()) {
			Fail(__LINE__, "first frame is not valid", 0.0, 1.0);
		}
		for (float value = 5.0f; value <= 12.0f; value = value + 1.0f) {
			frames.Push(value);
		}
		// Samples 5 to 8 published a second frame, samples 9 to 12 were dropped because this frame's buffer is leased.
		for (uint32 index = 0; index < 4; index++) {
			if (first[index] != static_cast<float>(index + 1)) {
				Fail(__LINE__, "held frame changed while the next one filled", first[index], static_cast<double>(index + 1));
			}
		}
	}
	{
		FrameCollector<float, 4>::Frame second = frames.Latest();
		for (uint32 index = 0; index < 4; index++) {
			if (second[index] != static_cast<float>(index + 5)) {
				Fail(__LINE__, "second frame after the drop", second[index], static_cast<double>(index + 5));
			}
		}
	}

	for (float value = 13.0f; value <= 16.0f; value = value + 1.0f) {
		frames.Push(value);
	}
	{
		FrameCollector<float, 4>::Frame third = frames.Latest();
		for (uint32 index = 0; index < 4; index++) {
			if (third[index] != static_cast<float>(index + 13)) {
				Fail(__LINE__, "frame after the lease ended", third[index], static_cast<double>(index + 13));
			}
		}
	}
	std::printf("FrameCollector hold: held frame stable, dropped frame skipped, next frame whole\n");
}


// FrameCollector with Hop 2 of Size 4: each frame keeps the last two samples of the previous one.
void CheckFrameHop() {
	FrameCollector<float, 4, 2> frames;
	for (float value = 1.0f; value <= 4.0f; value = value + 1.0f) {
		frames.Push(value);
	}
	const float expected[5][4] = {
		{ 1.0f, 2.0f, 3.0f, 4.0f },
		{ 3.0f, 4.0f, 5.0f, 6.0f },
		{ 5.0f, 6.0f, 7.0f, 8.0f },
		{ 7.0f, 8.0f, 9.0f, 10.0f },
		{ 9.0f, 10.0f, 11.0f, 12.0f },
	};
	for (uint32 frameIndex = 0; frameIndex < 5; frameIndex++) {
		if (frameIndex > 0) {
			for (uint32 step = 0; step < 2; step++) {
				float value = static_cast<float>(4 + (frameIndex - 1) * 2 + step + 1);
				frames.Push(value);
			}
		}
		FrameCollector<float, 4, 2>::Frame frame = frames.Latest();
		if (!frame.Valid()) {
			Fail(__LINE__, "hop frame is not valid", 0.0, 1.0);
			continue;
		}
		for (uint32 index = 0; index < 4; index++) {
			if (frame[index] != expected[frameIndex][index]) {
				Fail(__LINE__, "hop frame sample", frame[index], expected[frameIndex][index]);
			}
		}
	}
	std::printf("FrameCollector hop 2 of 4: five overlapping frames, each keeps two samples of the last\n");
}


// A second Latest while a frame is held returns an invalid frame, and destroying that invalid frame leaves the held lease in
// place. The held frame stays stable while the writer fills the other buffer and then tries to reuse the held one.
void CheckFrameSecondLatest() {
	FrameCollector<float, 4> frames;
	for (float value = 1.0f; value <= 4.0f; value = value + 1.0f) {
		frames.Push(value);
	}
	FrameCollector<float, 4>::Frame first = frames.Latest();
	if (!first.Valid()) {
		Fail(__LINE__, "first frame is not valid", 0.0, 1.0);
	}
	{
		FrameCollector<float, 4>::Frame second = frames.Latest();
		if (second.Valid()) {
			Fail(__LINE__, "second Latest while a frame is held is valid", 1.0, 0.0);
		}
	}
	for (float value = 5.0f; value <= 12.0f; value = value + 1.0f) {
		frames.Push(value);
	}
	for (uint32 index = 0; index < 4; index++) {
		if (first[index] != static_cast<float>(index + 1)) {
			Fail(__LINE__, "held frame changed after a second Latest was destroyed", first[index], static_cast<double>(index + 1));
		}
	}
	std::printf("FrameCollector second Latest: invalid while a frame is held, the held frame stays stable\n");
}


// The compile-time constants of the sources.
void CheckConstants() {
	if (AdcSamples<float, 12>::unitCodes != 2048) {
		Fail(__LINE__, "unitCodes of a 12-bit ADC", AdcSamples<float, 12>::unitCodes, 2048.0);
	}
	if (LatestSamples<float, 8>::size != 8) {
		Fail(__LINE__, "LatestSamples size", LatestSamples<float, 8>::size, 8.0);
	}
	if (FrameCollector<float, 8, 4>::hop != 4 || FrameCollector<float, 8, 4>::size != 8) {
		Fail(__LINE__, "FrameCollector size and hop", FrameCollector<float, 8, 4>::hop, 4.0);
	}
}


}


int main() {
	CheckSourcesAgree<float, 256>("float");
	CheckSourcesAgree<iq16, 256>("iq16");
	CheckSourcesAgree<iq24, 1024>("iq24");
	CheckAdcAgrees<float, 256>("float");
	CheckAdcAgrees<iq24, 1024>("iq24");
	CheckAdcEnds();
	CheckLatestWindow();
	CheckFrameHold();
	CheckFrameHop();
	CheckFrameSecondLatest();
	CheckConstants();

	if (failureCount != 0) {
		std::fprintf(stderr, "TestSampleSources: %d failure(s)\n", failureCount);
		return 1;
	}
	std::printf("TestSampleSources: all checks passed\n");
	return 0;
}
