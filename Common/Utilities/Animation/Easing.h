#pragma once
#include <Utilities/Math/IQMath/IQ.h>


template <RealType Type = float>
struct Easing {
	enum class Curve { Linear, EaseIn, EaseOut, EaseInOut };

	static Type Linear(Type t) {
		return t;
	}

	static Type EaseIn(Type t) {
		return t * t;
	}

	static Type EaseOut(Type t) {
		return t * (Type(2) - t);
	}

	static Type EaseInOut(Type t) {
		Type half(0.5f);
		return t < half ? Type(2) * t * t : Type(-1) + (Type(4) - Type(2) * t) * t;
	}

	static Type Apply(Curve curve, Type t) {
		switch (curve) {
			case Curve::EaseIn:    return EaseIn(t);
			case Curve::EaseOut:   return EaseOut(t);
			case Curve::EaseInOut: return EaseInOut(t);
			default:               return Linear(t);
		}
	}
};
