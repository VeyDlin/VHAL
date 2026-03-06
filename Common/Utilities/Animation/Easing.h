#pragma once


struct Easing {
	enum class Type { Linear, EaseIn, EaseOut, EaseInOut };

	static float Linear(float t) {
		return t;
	}

	static float EaseIn(float t) {
		return t * t;
	}

	static float EaseOut(float t) {
		return t * (2.0f - t);
	}

	static float EaseInOut(float t) {
		return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
	}

	static float Apply(Type type, float t) {
		switch (type) {
			case Type::EaseIn:    return EaseIn(t);
			case Type::EaseOut:   return EaseOut(t);
			case Type::EaseInOut: return EaseInOut(t);
			default:              return Linear(t);
		}
	}
};
