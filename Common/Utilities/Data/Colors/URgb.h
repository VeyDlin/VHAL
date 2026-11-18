#pragma once
#include <VHAL.h>
#include <cmath>
#include "FRgb.h"


namespace Colors {

	struct URgb {
		uint8 r = 0;
		uint8 g = 0;
		uint8 b = 0;

		constexpr URgb() = default;
		constexpr URgb(uint8 _r, uint8 _g, uint8 _b) : r(_r), g(_g), b(_b) {}

		URgb(const FRgb& rgb)
			: r(ClampToByte(rgb.r * 255.0f))
			, g(ClampToByte(rgb.g * 255.0f))
			, b(ClampToByte(rgb.b * 255.0f))
		{}

		URgb operator+(const URgb& o) const {
			return {AddSaturated(r, o.r), AddSaturated(g, o.g), AddSaturated(b, o.b)};
		}
		URgb operator-(const URgb& o) const {
			return {SubSaturated(r, o.r), SubSaturated(g, o.g), SubSaturated(b, o.b)};
		}
		URgb operator*(float s) const {
			return {
				ClampToByte(r * s),
				ClampToByte(g * s),
				ClampToByte(b * s)
			};
		}

		URgb& operator+=(const URgb& o) {
			r = AddSaturated(r, o.r);
			g = AddSaturated(g, o.g);
			b = AddSaturated(b, o.b);
			return *this;
		}
		URgb& operator-=(const URgb& o) {
			r = SubSaturated(r, o.r);
			g = SubSaturated(g, o.g);
			b = SubSaturated(b, o.b);
			return *this;
		}
		URgb& operator*=(float s) {
			r = ClampToByte(r * s);
			g = ClampToByte(g * s);
			b = ClampToByte(b * s);
			return *this;
		}

		constexpr bool operator==(const URgb& o) const { return r == o.r && g == o.g && b == o.b; }
		constexpr bool operator!=(const URgb& o) const { return !(*this == o); }

		float Magnitude() const {
			float fr = static_cast<float>(r);
			float fg = static_cast<float>(g);
			float fb = static_cast<float>(b);
			return std::sqrt(fr * fr + fg * fg + fb * fb);
		}

		URgb Normalized() const {
			float mag = Magnitude();
			if (mag < 0.0001f) return {};
			return *this * (1.0f / mag);
		}

		static constexpr URgb Black()   { return {0x00, 0x00, 0x00}; }
		static constexpr URgb Red()     { return {0xFF, 0x00, 0x00}; }
		static constexpr URgb Green()   { return {0x00, 0xFF, 0x00}; }
		static constexpr URgb Blue()    { return {0x00, 0x00, 0xFF}; }
		static constexpr URgb Yellow()  { return {0xFF, 0xFF, 0x00}; }
		static constexpr URgb Cyan()    { return {0x00, 0xFF, 0xFF}; }
		static constexpr URgb Magenta() { return {0xFF, 0x00, 0xFF}; }
		static constexpr URgb White()   { return {0xFF, 0xFF, 0xFF}; }

	private:
		static uint8 AddSaturated(uint8 a, uint8 b) {
			int sum = a + b;
			return sum > 255 ? uint8(255) : uint8(sum);
		}

		static uint8 SubSaturated(uint8 a, uint8 b) {
			return a > b ? uint8(a - b) : uint8(0);
		}

		// Rounds to nearest and saturates; written so NaN maps to 0
		static uint8 ClampToByte(float value) {
			if (!(value > 0.0f)) {
				return 0;
			}
			if (value >= 255.0f) {
				return 255;
			}
			return static_cast<uint8>(value + 0.5f);
		}
	};


	// Deferred definition: FRgb(const URgb&)
	inline FRgb::FRgb(const URgb& rgb)
		: r(rgb.r / 255.0f)
		, g(rgb.g / 255.0f)
		, b(rgb.b / 255.0f)
	{}

}
