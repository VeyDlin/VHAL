#pragma once
#include <VHAL.h>
#include <cmath>


namespace Colors {
	template<typename Type>
	struct IRgb {
	public:
		Type r = 0;
		Type g = 0;
		Type b = 0;

		IRgb() {}

		IRgb(Type _r, Type _g, Type _b): r(_r),  g(_g), b(_b) { }

		template<typename InType>
		IRgb(IRgb<InType> &rgb) {
			r = static_cast<Type>(rgb.r);
			g = static_cast<Type>(rgb.g);
			b = static_cast<Type>(rgb.b);
		}

		template<typename InType>
		void Add(IRgb<InType> &rgb) {
			r += static_cast<Type>(rgb.r);
			g += static_cast<Type>(rgb.g);
			b += static_cast<Type>(rgb.b);
		}

		bool Compare(IRgb<Type> &rgb) {
			return rgb.r == r && rgb.g == g && rgb.b == b;
		}

		IRgb operator+(const IRgb& other) const {
			return IRgb(r + other.r, g + other.g, b + other.b);
		}

		IRgb operator-(const IRgb& other) const {
			return IRgb(r - other.r, g - other.g, b - other.b);
		}

		IRgb operator*(float scalar) const {
			return IRgb(
				static_cast<Type>(r * scalar),
				static_cast<Type>(g * scalar),
				static_cast<Type>(b * scalar)
			);
		}

		float Magnitude() const {
			float fr = static_cast<float>(r);
			float fg = static_cast<float>(g);
			float fb = static_cast<float>(b);
			return std::sqrt(fr * fr + fg * fg + fb * fb);
		}

		IRgb Normalized() const {
			float mag = Magnitude();
			if (mag < 0.0001f) return IRgb{};
			return *this * (1.0f / mag);
		}
	};


	struct FRgb : public IRgb<float> {
		FRgb() {}

		FRgb(float _r, float _g, float _b): IRgb::IRgb(_r, _g, _b) { }

		template<typename InType>
		FRgb(IRgb<InType> &rgb): IRgb::IRgb(rgb) { }

		FRgb operator+(const FRgb& other) const {
			return FRgb(r + other.r, g + other.g, b + other.b);
		}

		FRgb operator-(const FRgb& other) const {
			return FRgb(r - other.r, g - other.g, b - other.b);
		}

		FRgb operator*(float scalar) const {
			return FRgb(r * scalar, g * scalar, b * scalar);
		}

		static FRgb Black() 	{ static const FRgb c = { 0, 0, 0 }; return c; }
		static FRgb Red() 		{ static const FRgb c = { 1, 0, 0 }; return c; }
		static FRgb Green() 	{ static const FRgb c = { 0, 1, 0 }; return c; }
		static FRgb Blue() 		{ static const FRgb c = { 0, 0, 1 }; return c; }
		static FRgb Yellow() 	{ static const FRgb c = { 1, 1, 0 }; return c; }
		static FRgb Cyan() 		{ static const FRgb c = { 0, 1, 1 }; return c; }
		static FRgb Magenta() 	{ static const FRgb c = { 1, 0, 1 }; return c; }
		static FRgb White() 	{ static const FRgb c = { 1, 1, 1 }; return c; }
	};


	struct URgb : public IRgb<uint8> {
		URgb() {}

		URgb(uint8 _r, uint8 _g, uint8 _b): IRgb::IRgb(_r, _g, _b) { }

		template<typename InType>
		URgb(IRgb<InType> &rgb): IRgb::IRgb(rgb) { }

		static URgb Black() 	{ static const URgb c = { 0x00, 0x00, 0x00 }; return c; }
		static URgb Red() 		{ static const URgb c = { 0xFF, 0x00, 0x00 }; return c; }
		static URgb Green() 	{ static const URgb c = { 0x00, 0xFF, 0x00 }; return c; }
		static URgb Blue() 		{ static const URgb c = { 0x00, 0x00, 0xFF }; return c; }
		static URgb Yellow() 	{ static const URgb c = { 0xFF, 0xFF, 0x00 }; return c; }
		static URgb Cyan() 		{ static const URgb c = { 0x00, 0xFF, 0xFF }; return c; }
		static URgb Magenta() 	{ static const URgb c = { 0xFF, 0x00, 0xFF }; return c; }
		static URgb White() 	{ static const URgb c = { 0xFF, 0xFF, 0xFF }; return c; }
	};
};