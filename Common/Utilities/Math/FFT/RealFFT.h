#pragma once
#include <Utilities/DataTypes.h>
#include <cmath>


// Radix-2 FFT of a real signal of Size samples (a power of two, at least 4). The signal is packed into a complex FFT of
// Size / 2 points and unpacked afterwards, so the work is about half that of a complex transform of Size points. The
// twiddle factors, the Hann window and the bit-reversal table are computed once in the constructor. The class holds all
// its buffers, so a transform allocates nothing and needs no library: it runs the same on every MCU.
template <uint32 Size>
class RealFFT {
	static_assert(Size >= 4 && (Size & (Size - 1)) == 0, "RealFFT: Size must be a power of two, at least 4");

public:
	struct Complex {
		float re = 0.0f;
		float im = 0.0f;
	};


	static constexpr uint32 size = Size;
	// Bins 0 (DC) to Size / 2 (the Nyquist frequency).
	static constexpr uint32 binCount = Size / 2 + 1;
	// A sine of amplitude A at a bin centre has |X| = A * Size / 4 under the Hann window, so Analyze multiplies by this.
	static constexpr float amplitudeScale = 4.0f / static_cast<float>(Size);


private:
	static constexpr uint32 halfSize = Size / 2;
	static constexpr float pi = 3.14159265358979323846f;

	float window[Size] = {};
	float twiddleRe[halfSize] = {};
	float twiddleIm[halfSize] = {};
	uint16 bitReverse[halfSize] = {};
	Complex work[halfSize] = {};
	Complex bins[binCount] = {};


public:
	RealFFT() {
		for (uint32 index = 0; index < Size; index++) {
			window[index] = 0.5f - 0.5f * std::cos(2.0f * pi * static_cast<float>(index) / static_cast<float>(Size));
		}

		for (uint32 index = 0; index < halfSize; index++) {
			float angle = 2.0f * pi * static_cast<float>(index) / static_cast<float>(Size);
			twiddleRe[index] = std::cos(angle);
			twiddleIm[index] = -std::sin(angle);
		}

		uint32 bits = 0;
		while ((1u << bits) < halfSize) {
			bits++;
		}

		for (uint32 index = 0; index < halfSize; index++) {
			uint32 reversed = 0;
			for (uint32 bit = 0; bit < bits; bit++) {
				reversed = (reversed << 1) | ((index >> bit) & 1u);
			}

			bitReverse[index] = static_cast<uint16>(reversed);
		}
	}


	// The DFT of the unwindowed signal: input holds Size samples, spectrum receives binCount bins.
	void Forward(const float *input, Complex *spectrum) {
		Load(input, nullptr);
		Butterflies();
		Unpack(spectrum);
	}


	// The Hann-windowed amplitude spectrum: amplitude receives binCount values, each the amplitude of a sine at that bin.
	// A silent input gives zeros.
	void Analyze(const float *samples, float *amplitude) {
		Load(samples, window);
		Butterflies();
		Unpack(bins);
		for (uint32 index = 0; index < binCount; index++) {
			float magnitude = std::sqrt(bins[index].re * bins[index].re + bins[index].im * bins[index].im);
			amplitude[index] = magnitude * amplitudeScale;
		}
	}


private:
	// Packs sample pairs into the complex work array in bit-reversed order, optionally multiplied by a taper.
	void Load(const float *input, const float *taper) {
		for (uint32 index = 0; index < halfSize; index++) {
			uint32 first = 2 * index;
			uint32 second = first + 1;
			float realPart = taper == nullptr ? input[first] : input[first] * taper[first];
			float imagPart = taper == nullptr ? input[second] : input[second] * taper[second];
			work[bitReverse[index]].re = realPart;
			work[bitReverse[index]].im = imagPart;
		}
	}


	// Iterative decimation-in-time butterflies on the packed signal. The twiddle of a stage of length L is taken from the
	// table of the full Size-point circle, so one table serves every stage.
	void Butterflies() {
		for (uint32 length = 2; length <= halfSize; length = length * 2) {
			uint32 half = length / 2;
			uint32 stride = Size / length;
			for (uint32 start = 0; start < halfSize; start = start + length) {
				for (uint32 offset = 0; offset < half; offset++) {
					uint32 twiddle = offset * stride;
					Complex &low = work[start + offset];
					Complex &high = work[start + offset + half];
					float productRe = twiddleRe[twiddle] * high.re - twiddleIm[twiddle] * high.im;
					float productIm = twiddleRe[twiddle] * high.im + twiddleIm[twiddle] * high.re;
					high.re = low.re - productRe;
					high.im = low.im - productIm;
					low.re = low.re + productRe;
					low.im = low.im + productIm;
				}
			}
		}
	}


	// Splits the packed transform Z into the spectrum of the real signal: X[k] = E[k] + W^k O[k], where E and O are the
	// transforms of the even and odd samples. Z[M] is taken as Z[0] (M = Size / 2).
	void Unpack(Complex *spectrum) {
		for (uint32 bin = 0; bin < binCount; bin++) {
			uint32 direct = bin % halfSize;
			uint32 mirror = (halfSize - bin) % halfSize;
			float evenRe = 0.5f * (work[direct].re + work[mirror].re);
			float evenIm = 0.5f * (work[direct].im - work[mirror].im);
			float diffRe = work[direct].re - work[mirror].re;
			float diffIm = work[direct].im + work[mirror].im;
			float oddRe = 0.5f * diffIm;
			float oddIm = -0.5f * diffRe;

			float twiddleRealPart = -1.0f;
			float twiddleImagPart = 0.0f;
			if (bin < halfSize) {
				twiddleRealPart = twiddleRe[bin];
				twiddleImagPart = twiddleIm[bin];
			}

			spectrum[bin].re = evenRe + twiddleRealPart * oddRe - twiddleImagPart * oddIm;
			spectrum[bin].im = evenIm + twiddleRealPart * oddIm + twiddleImagPart * oddRe;
		}
	}
};
