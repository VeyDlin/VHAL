#pragma once
#include <Utilities/DataTypes.h>
#include <Utilities/Math/FFT/SampleSource.h>
#include <Utilities/Math/IQMath/IQ.h>
#include <cmath>
#include <type_traits>


// A complex value of the number type Number (float, double or IQ<Q>). Both parts start at zero.
template <typename Number>
struct Complex {
	Number re = {};
	Number im = {};
};


namespace FFTDetail {
	constexpr double pi = 3.14159265358979323846264338327950288;
	constexpr double halfPi = pi / 2.0;
	constexpr double twoPowerThirty = 1073741824.0;


	struct CircleValue {
		double sine = 0.0;
		double cosine = 0.0;
	};


	// Sine of an angle in [0, pi / 2] by its Taylor series. The terms through x^19 leave an error below 1e-13.
	constexpr double SineOnQuarter(double angle) {
		double squared = angle * angle;
		double term = angle;
		double sum = angle;
		for (uint32 order = 1; order <= 9; order++) {
			double denominator = static_cast<double>((2 * order) * (2 * order + 1));
			term = -term * squared / denominator;
			sum = sum + term;
		}
		return sum;
	}


	// Sine and cosine of the angle 2 pi numerator / denominator, accurate on the whole circle. The angle is first reduced to
	// a quadrant and an offset in [0, pi / 2) with integer arithmetic, so the series only sees small angles, and exact
	// quarter turns give exact zeros and ones.
	constexpr CircleValue CircleSinCos(uint32 numerator, uint32 denominator) {
		uint64 quarters = 4 * static_cast<uint64>(numerator % denominator);
		uint64 quadrant = quarters / denominator;
		uint64 remainder = quarters % denominator;

		double sineOffset = 0.0;
		double cosineOffset = 1.0;
		if (remainder != 0) {
			double offset = halfPi * static_cast<double>(remainder) / static_cast<double>(denominator);
			sineOffset = SineOnQuarter(offset);
			cosineOffset = SineOnQuarter(halfPi - offset);
		}

		if (quadrant == 0) {
			return { sineOffset, cosineOffset };
		}
		if (quadrant == 1) {
			return { cosineOffset, -sineOffset };
		}
		if (quadrant == 2) {
			return { -sineOffset, -cosineOffset };
		}
		return { -cosineOffset, sineOffset };
	}


	// The stored form of a real constant: float and double round to their own type, and an IQ takes the nearest raw value.
	template <typename Number>
	constexpr Number FromReal(double value) {
		if constexpr (IQType<Number>) {
			return Number::FromRaw(IQDetail::RealToRaw(value * static_cast<double>(Number::one)));
		} else {
			return static_cast<Number>(value);
		}
	}


	// Twiddle factors are stored as int32 with 30 fraction bits in IQ, and in their own type otherwise.
	template <typename Number>
	using TwiddleType = std::conditional_t<IQType<Number>, int32, Number>;


	template <typename Number>
	constexpr TwiddleType<Number> ToTwiddle(double value) {
		if constexpr (IQType<Number>) {
			double scaled = value * twoPowerThirty;
			return static_cast<int32>(static_cast<int64>(scaled >= 0.0 ? scaled + 0.5 : scaled - 0.5));
		} else {
			return static_cast<Number>(value);
		}
	}


	// Log2 of a power of two.
	constexpr uint32 Log2Of(uint32 value) {
		uint32 bits = 0;
		while ((1u << bits) < value) {
			bits++;
		}
		return bits;
	}


	// A spectrum bin can reach 2, so an IQ must have a Q up to 29 to hold it. Other types have no Q to check.
	template <typename Number>
	constexpr bool SpectrumFits() {
		if constexpr (IQType<Number>) {
			return Number::q <= 29;
		} else {
			return true;
		}
	}


	// Twiddle factors and the bit-reversal permutation of the packed Size / 2 point transform. Built at compile time, so
	// they are constant data in flash.
	template <uint32 Size, typename Number>
	struct Tables {
		static constexpr uint32 halfSize = Size / 2;

		TwiddleType<Number> re[halfSize] = {};
		TwiddleType<Number> im[halfSize] = {};
		uint16 bitReverse[halfSize] = {};

		constexpr Tables() {
			for (uint32 index = 0; index < halfSize; index++) {
				CircleValue circle = CircleSinCos(index, Size);
				re[index] = ToTwiddle<Number>(circle.cosine);
				im[index] = ToTwiddle<Number>(-circle.sine);
			}

			uint32 bits = Log2Of(halfSize);
			for (uint32 index = 0; index < halfSize; index++) {
				uint32 reversed = 0;
				for (uint32 bit = 0; bit < bits; bit++) {
					reversed = (reversed << 1) | ((index >> bit) & 1u);
				}
				bitReverse[index] = static_cast<uint16>(reversed);
			}
		}
	};


	template <uint32 Size, typename Number>
	inline constexpr Tables<Size, Number> fftTables{};


	// Rounds a value divided by 2^30 to the nearest integer, ties toward +infinity.
	constexpr int64 RoundShift30(int64 value) {
		return (value + (int64(1) << 29)) >> 30;
	}


	// Halves a value, rounding to the nearest integer, ties toward +infinity.
	constexpr int64 RoundHalf(int64 value) {
		return (value + 1) >> 1;
	}


	// Floor of the square root, by the bit-by-bit method, which needs no division.
	constexpr uint64 IntegerSqrt(uint64 value) {
		uint64 result = 0;
		uint64 bit = uint64(1) << 62;
		while (bit > value) {
			bit = bit >> 2;
		}
		while (bit != 0) {
			if (value >= result + bit) {
				value = value - (result + bit);
				result = (result >> 1) + bit;
			} else {
				result = result >> 1;
			}
			bit = bit >> 2;
		}
		return result;
	}
}


// The Hann window, 0.5 - 0.5 cos(2 pi n / Size). Its table is generated at compile time.
struct HannWindow {
	static constexpr bool tapered = true;
	// The mean of the window: a sine at a bin centre loses this factor in its spectrum.
	static constexpr double coherentGain = 0.5;


	template <uint32 Size, typename Number>
	struct Table {
		Number values[Size] = {};

		constexpr Table() {
			for (uint32 index = 0; index < Size; index++) {
				FFTDetail::CircleValue circle = FFTDetail::CircleSinCos(index, Size);
				values[index] = FFTDetail::FromReal<Number>(0.5 - 0.5 * circle.cosine);
			}
		}
	};


	template <uint32 Size, typename Number>
	static inline constexpr Table<Size, Number> table{};
};


// No window: every sample counts once. Its table is all ones, and an Analyze with this window never reads it.
struct RectangularWindow {
	static constexpr bool tapered = false;
	static constexpr double coherentGain = 1.0;


	template <uint32 Size, typename Number>
	struct Table {
		Number values[Size] = {};

		constexpr Table() {
			for (uint32 index = 0; index < Size; index++) {
				values[index] = FFTDetail::FromReal<Number>(1.0);
			}
		}
	};


	template <uint32 Size, typename Number>
	static inline constexpr Table<Size, Number> table{};
};


// Radix-2 FFT of a real signal of Size samples (a power of two, at least 4). The signal is packed into a complex FFT of
// Size / 2 points and unpacked afterwards, so the work is about half that of a complex transform of Size points.
//
// Number is float, double or IQ<Q>. With IQ, every butterfly stage halves its outputs with rounding, so a full-scale
// input cannot overflow, and the spectrum from Forward is scaled by spectrumScale. Window is HannWindow or
// RectangularWindow, or any type with the same members. The tables are constant data in flash and the class holds only
// the packed work array, so a transform allocates nothing and needs no library.
template <uint32 Size, typename Number = float, typename Window = HannWindow>
class RealFFT {
	static_assert(Size >= 4 && (Size & (Size - 1)) == 0, "RealFFT: Size must be a power of two, at least 4");
	static_assert(Size <= 131072, "RealFFT: Size must be at most 2^17 for the 16-bit bit-reversal table");
	static_assert(std::is_floating_point_v<Number> || IQType<Number>, "RealFFT: Number must be float, double or IQ<Q>");

public:
	static constexpr uint32 size = Size;
	// Bins 0 (DC) to Size / 2 (the Nyquist frequency).
	static constexpr uint32 binCount = Size / 2 + 1;
	// Forward returns the spectrum multiplied by this factor. IQ halves each of the log2(Size / 2) stages, so the total is
	// 1 / (Size / 2).
	static constexpr double spectrumScale = IQType<Number> ? 2.0 / static_cast<double>(Size) : 1.0;
	// Factor from a magnitude of the spectrum from Forward to the amplitude of a sine at its bin. A sine of amplitude A at a
	// bin centre has the magnitude A * Size * coherentGain / 2, so with the Hann window this is 4 / Size.
	static constexpr double amplitudeScale = 2.0 / (static_cast<double>(Size) * Window::coherentGain) / spectrumScale;


private:
	static constexpr uint32 halfSize = Size / 2;
	// The IQ amplitude factor with 30 fraction bits, as an integer for the 64-bit scaling in Amplitude.
	static constexpr uint64 amplitudeQ30 = IQType<Number> ? static_cast<uint64>(amplitudeScale * FFTDetail::twoPowerThirty + 0.5) : 0;
	static_assert(!IQType<Number> || amplitudeScale <= 2.0, "RealFFT: an IQ amplitude factor above 2 needs a wider product");
	static_assert(FFTDetail::SpectrumFits<Number>(), "RealFFT: IQ needs Q <= 29 to hold a spectrum bin of 2");

	Complex<Number> work[halfSize] = {};


public:
	// The DFT of the unwindowed signal: source gives Size samples, spectrum receives binCount bins.
	template <SampleSourceOf<Number> Source>
	void Forward(const Source &source, Complex<Number> *spectrum) {
		Load<Source, false>(source);
		Butterflies();
		for (uint32 bin = 0; bin < binCount; bin++) {
			spectrum[bin] = Bin(bin);
		}
	}


	// The windowed amplitude spectrum: amplitude receives binCount values, each the amplitude of a sine at that bin. A
	// silent input gives zeros. The source gives Size samples.
	template <SampleSourceOf<Number> Source>
	void Analyze(const Source &source, Number *amplitude) {
		Load<Source, Window::tapered>(source);
		Butterflies();
		for (uint32 bin = 0; bin < binCount; bin++) {
			amplitude[bin] = Amplitude(Bin(bin));
		}
	}


private:
	// Packs sample pairs into the work array in bit-reversed order. Taper multiplies by the window; it is a compile-time
	// flag, so a window that does not taper costs no table.
	template <typename Source, bool Taper>
	void Load(const Source &source) {
		for (uint32 index = 0; index < halfSize; index++) {
			uint32 first = 2 * index;
			uint32 second = first + 1;
			Number realPart = source[first];
			Number imagPart = source[second];
			if constexpr (Taper) {
				const Number *taper = Window::template table<Size, Number>.values;
				realPart = realPart * taper[first];
				imagPart = imagPart * taper[second];
			}
			uint16 target = FFTDetail::fftTables<Size, Number>.bitReverse[index];
			work[target].re = realPart;
			work[target].im = imagPart;
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
					Complex<Number> &low = work[start + offset];
					Complex<Number> &high = work[start + offset + half];
					if constexpr (IQType<Number>) {
						FixedButterfly(low, high, twiddle);
					} else {
						FloatButterfly(low, high, twiddle);
					}
				}
			}
		}
	}


	void FloatButterfly(Complex<Number> &low, Complex<Number> &high, uint32 twiddle) {
		const Number twiddleRe = FFTDetail::fftTables<Size, Number>.re[twiddle];
		const Number twiddleIm = FFTDetail::fftTables<Size, Number>.im[twiddle];
		Number productRe = twiddleRe * high.re - twiddleIm * high.im;
		Number productIm = twiddleRe * high.im + twiddleIm * high.re;
		high.re = low.re - productRe;
		high.im = low.im - productIm;
		low.re = low.re + productRe;
		low.im = low.im + productIm;
	}


	// The IQ butterfly: the twiddle product is a 64-bit value rounded from Q30, and both outputs are halved with rounding.
	void FixedButterfly(Complex<Number> &low, Complex<Number> &high, uint32 twiddle) {
		const int64 twiddleRe = FFTDetail::fftTables<Size, Number>.re[twiddle];
		const int64 twiddleIm = FFTDetail::fftTables<Size, Number>.im[twiddle];
		const int64 highRe = high.re.raw;
		const int64 highIm = high.im.raw;
		const int64 productRe = FFTDetail::RoundShift30(twiddleRe * highRe - twiddleIm * highIm);
		const int64 productIm = FFTDetail::RoundShift30(twiddleRe * highIm + twiddleIm * highRe);
		const int64 lowRe = low.re.raw;
		const int64 lowIm = low.im.raw;
		high.re = Number::FromRaw(IQDetail::SaturateRaw(FFTDetail::RoundHalf(lowRe - productRe)));
		high.im = Number::FromRaw(IQDetail::SaturateRaw(FFTDetail::RoundHalf(lowIm - productIm)));
		low.re = Number::FromRaw(IQDetail::SaturateRaw(FFTDetail::RoundHalf(lowRe + productRe)));
		low.im = Number::FromRaw(IQDetail::SaturateRaw(FFTDetail::RoundHalf(lowIm + productIm)));
	}


	// One bin of the real spectrum from the packed transform Z: X[k] = E[k] + W^k O[k], where E and O are the transforms of
	// the even and odd samples. Z[M] is taken as Z[0] (M = Size / 2), and the bin M uses the twiddle -1.
	Complex<Number> Bin(uint32 bin) const {
		if constexpr (IQType<Number>) {
			return FixedBin(bin);
		} else {
			return FloatBin(bin);
		}
	}


	Complex<Number> FloatBin(uint32 bin) const {
		uint32 direct = bin % halfSize;
		uint32 mirror = (halfSize - bin) % halfSize;
		const Number half = FFTDetail::FromReal<Number>(0.5);
		Number evenRe = half * (work[direct].re + work[mirror].re);
		Number evenIm = half * (work[direct].im - work[mirror].im);
		Number diffRe = work[direct].re - work[mirror].re;
		Number diffIm = work[direct].im + work[mirror].im;
		Number oddRe = half * diffIm;
		Number oddIm = -half * diffRe;

		Number twiddleRe = FFTDetail::FromReal<Number>(-1.0);
		Number twiddleIm = FFTDetail::FromReal<Number>(0.0);
		if (bin < halfSize) {
			twiddleRe = FFTDetail::fftTables<Size, Number>.re[bin];
			twiddleIm = FFTDetail::fftTables<Size, Number>.im[bin];
		}

		Complex<Number> result;
		result.re = evenRe + twiddleRe * oddRe - twiddleIm * oddIm;
		result.im = evenIm + twiddleRe * oddIm + twiddleIm * oddRe;
		return result;
	}


	// The same unpacking in 64-bit raw values. The 0.5 factors are halvings with rounding, and the result saturates to the
	// range of IQ.
	Complex<Number> FixedBin(uint32 bin) const {
		uint32 direct = bin % halfSize;
		uint32 mirror = (halfSize - bin) % halfSize;
		const int64 directRe = work[direct].re.raw;
		const int64 directIm = work[direct].im.raw;
		const int64 mirrorRe = work[mirror].re.raw;
		const int64 mirrorIm = work[mirror].im.raw;
		const int64 evenRe = FFTDetail::RoundHalf(directRe + mirrorRe);
		const int64 evenIm = FFTDetail::RoundHalf(directIm - mirrorIm);
		const int64 oddRe = FFTDetail::RoundHalf(directIm + mirrorIm);
		const int64 oddIm = FFTDetail::RoundHalf(mirrorRe - directRe);

		int64 twiddleRe = -(int64(1) << 30);
		int64 twiddleIm = 0;
		if (bin < halfSize) {
			twiddleRe = FFTDetail::fftTables<Size, Number>.re[bin];
			twiddleIm = FFTDetail::fftTables<Size, Number>.im[bin];
		}

		const int64 sumRe = evenRe + FFTDetail::RoundShift30(twiddleRe * oddRe - twiddleIm * oddIm);
		const int64 sumIm = evenIm + FFTDetail::RoundShift30(twiddleRe * oddIm + twiddleIm * oddRe);
		Complex<Number> result;
		result.re = Number::FromRaw(IQDetail::SaturateRaw(sumRe));
		result.im = Number::FromRaw(IQDetail::SaturateRaw(sumIm));
		return result;
	}


	// Magnitude of a bin times the amplitude factor. IQ takes the 64-bit squared magnitude and its integer square root,
	// rounded to the nearest raw value, so the magnitude is exact to one LSB of the spectrum.
	Number Amplitude(Complex<Number> value) const {
		if constexpr (IQType<Number>) {
			const int64 re = value.re.raw;
			const int64 im = value.im.raw;
			const uint64 squared = static_cast<uint64>(re * re) + static_cast<uint64>(im * im);
			uint64 magnitude = FFTDetail::IntegerSqrt(squared);
			if (squared - magnitude * magnitude > magnitude) {
				magnitude++;
			}
			const uint64 scaled = (magnitude * amplitudeQ30 + (uint64(1) << 29)) >> 30;
			if (scaled > static_cast<uint64>(IQDetail::maxRaw)) {
				return Number::FromRaw(IQDetail::maxRaw);
			}
			return Number::FromRaw(static_cast<int32>(scaled));
		} else {
			const Number scale = FFTDetail::FromReal<Number>(amplitudeScale);
			return std::sqrt(value.re * value.re + value.im * value.im) * scale;
		}
	}
};
