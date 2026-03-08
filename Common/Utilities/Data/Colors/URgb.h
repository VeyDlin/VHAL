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
			: r(static_cast<uint8>(rgb.r * 255.0f + 0.5f))
			, g(static_cast<uint8>(rgb.g * 255.0f + 0.5f))
			, b(static_cast<uint8>(rgb.b * 255.0f + 0.5f))
		{}

		URgb operator+(const URgb& o) const { return {uint8(r + o.r), uint8(g + o.g), uint8(b + o.b)}; }
		URgb operator-(const URgb& o) const { return {uint8(r - o.r), uint8(g - o.g), uint8(b - o.b)}; }
		URgb operator*(float s) const {
			return {
				static_cast<uint8>(r * s + 0.5f),
				static_cast<uint8>(g * s + 0.5f),
				static_cast<uint8>(b * s + 0.5f)
			};
		}

		URgb& operator+=(const URgb& o) { r += o.r; g += o.g; b += o.b; return *this; }
		URgb& operator-=(const URgb& o) { r -= o.r; g -= o.g; b -= o.b; return *this; }
		URgb& operator*=(float s) {
			r = static_cast<uint8>(r * s + 0.5f);
			g = static_cast<uint8>(g * s + 0.5f);
			b = static_cast<uint8>(b * s + 0.5f);
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
	};


	// Deferred definition: FRgb(const URgb&)
	inline FRgb::FRgb(const URgb& rgb)
		: r(rgb.r / 255.0f)
		, g(rgb.g / 255.0f)
		, b(rgb.b / 255.0f)
	{}

}
