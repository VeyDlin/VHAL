#pragma once
#include <cmath>


namespace Colors {

	struct URgb;

	struct FRgb {
		float r = 0;
		float g = 0;
		float b = 0;

		constexpr FRgb() = default;
		constexpr FRgb(float _r, float _g, float _b) : r(_r), g(_g), b(_b) {}
		inline FRgb(const URgb& rgb);

		constexpr FRgb operator+(const FRgb& o) const { return {r + o.r, g + o.g, b + o.b}; }
		constexpr FRgb operator-(const FRgb& o) const { return {r - o.r, g - o.g, b - o.b}; }
		constexpr FRgb operator*(float s) const { return {r * s, g * s, b * s}; }

		FRgb& operator+=(const FRgb& o) { r += o.r; g += o.g; b += o.b; return *this; }
		FRgb& operator-=(const FRgb& o) { r -= o.r; g -= o.g; b -= o.b; return *this; }
		FRgb& operator*=(float s) { r *= s; g *= s; b *= s; return *this; }

		constexpr bool operator==(const FRgb& o) const { return r == o.r && g == o.g && b == o.b; }
		constexpr bool operator!=(const FRgb& o) const { return !(*this == o); }

		float Magnitude() const {
			return std::sqrt(r * r + g * g + b * b);
		}

		FRgb Normalized() const {
			float mag = Magnitude();
			if (mag < 0.0001f) return {};
			return *this * (1.0f / mag);
		}

		static constexpr FRgb Black()   { return {0, 0, 0}; }
		static constexpr FRgb Red()     { return {1, 0, 0}; }
		static constexpr FRgb Green()   { return {0, 1, 0}; }
		static constexpr FRgb Blue()    { return {0, 0, 1}; }
		static constexpr FRgb Yellow()  { return {1, 1, 0}; }
		static constexpr FRgb Cyan()    { return {0, 1, 1}; }
		static constexpr FRgb Magenta() { return {1, 0, 1}; }
		static constexpr FRgb White()   { return {1, 1, 1}; }
	};

}
