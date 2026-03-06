#pragma once
#include <functional>
#include <chrono>
#include <cmath>
#include <type_traits>
#include "Easing.h"


template<typename T>
class Animation {
public:
	enum class Mode { None, Tween, Spring };

	// === Callbacks ===
	std::function<void(T)> onUpdateValue;
	std::function<void(T)> onStart;
	std::function<void(T)> onStop;


private:
	Mode activeMode = Mode::None;
	bool paused = false;

	T value{};
	T target{};

	// --- Tween state ---
	T tweenFrom{};
	uint64_t startTick = 0;
	uint64_t durationMs = 0;

	Easing::Type easingType = Easing::Type::Linear;
	std::function<float(float)> easingFunction;

	// --- Spring state ---
	float springStiffness = 200.0f;
	float springDamping = 20.0f;
	float springVelocity = 0.0f;
	float springEpsilon = 0.001f;

	// --- Time tracking ---
	uint64_t lastTick = 0;


public:
	// ==================== Tween ====================

	void Tween(T to, std::chrono::milliseconds duration) {
		TweenInternal(value, to, duration.count());
	}

	void Tween(T from, T to, std::chrono::milliseconds duration) {
		TweenInternal(from, to, duration.count());
	}

	void TweenSpeed(T to, float speed) {
		float distance = Distance(value, to);
		uint64_t ms = (speed > 0) ? static_cast<uint64_t>((distance / speed) * 1000.0f) : 0;
		TweenInternal(value, to, ms);
	}

	void TweenSpeed(T from, T to, float speed) {
		float distance = Distance(from, to);
		uint64_t ms = (speed > 0) ? static_cast<uint64_t>((distance / speed) * 1000.0f) : 0;
		TweenInternal(from, to, ms);
	}


	// ==================== Spring ====================

	void SetSpring(float stiffness, float damping) {
		springStiffness = stiffness;
		springDamping = damping;
	}

	void SetSpringEpsilon(float eps) {
		springEpsilon = eps;
	}

	void SetTarget(T newTarget) {
		target = newTarget;

		if (activeMode != Mode::Spring) {
			if (activeMode == Mode::None && onStart) {
				onStart(value);
			}
			activeMode = Mode::Spring;
			springVelocity = 0.0f;
		}
	}


	// ==================== Easing ====================

	void SetEasing(Easing::Type type) {
		easingType = type;
		easingFunction = nullptr;
	}

	void SetEasing(std::function<float(float)> fn) {
		easingFunction = fn;
	}


	// ==================== Control ====================

	void Pause() {
		paused = true;
	}

	void Resume() {
		paused = false;
	}

	void Stop() {
		bool wasActive = activeMode != Mode::None;
		activeMode = Mode::None;
		paused = false;
		springVelocity = 0.0f;

		if (wasActive && onStop) {
			onStop(value);
		}
	}


	// ==================== Tick ====================

	void Update(uint64_t nowMs) {
		if (activeMode == Mode::None || paused) {
			lastTick = nowMs;
			return;
		}

		switch (activeMode) {
			case Mode::Tween:
				UpdateTween(nowMs);
				break;

			case Mode::Spring:
				UpdateSpring(nowMs);
				break;

			default:
				break;
		}

		lastTick = nowMs;
	}


	// ==================== State ====================

	bool IsActive() const {
		return activeMode != Mode::None;
	}

	Mode GetMode() const {
		return activeMode;
	}

	T GetValue() const {
		return value;
	}

	T GetEndValue() const {
		return target;
	}

	void SetValue(T val) {
		value = val;
	}


private:
	// ==================== Internal ====================

	void TweenInternal(T from, T to, uint64_t ms) {
		tweenFrom = from;
		target = to;
		value = from;
		durationMs = ms;
		startTick = 0;
		springVelocity = 0.0f;

		if (activeMode == Mode::None && onStart) {
			onStart(from);
		}

		activeMode = Mode::Tween;
	}


	void UpdateTween(uint64_t nowMs) {
		if (startTick == 0) {
			startTick = nowMs;
		}

		if (durationMs == 0) {
			value = target;
			if (onUpdateValue) {
				onUpdateValue(value);
			}
			FinishAnimation();
			return;
		}

		uint64_t elapsed = nowMs - startTick;
		float t = static_cast<float>(elapsed) / static_cast<float>(durationMs);

		if (t >= 1.0f) {
			t = 1.0f;
		}

		float easedT = easingFunction ? easingFunction(t) : Easing::Apply(easingType, t);

		value = Lerp(tweenFrom, target, easedT);

		if (onUpdateValue) {
			onUpdateValue(value);
		}

		if (t >= 1.0f) {
			FinishAnimation();
		}
	}


	void UpdateSpring(uint64_t nowMs) {
		float dt;

		if (lastTick == 0 || lastTick >= nowMs) {
			dt = 0.001f;
		} else {
			dt = static_cast<float>(nowMs - lastTick) / 1000.0f;
		}

		if (dt > 0.1f) {
			dt = 0.1f;
		}

		float diff = ScalarDiff(target, value);
		float force = diff * springStiffness;
		springVelocity += force * dt;
		springVelocity *= (1.0f - springDamping * dt);

		value = Advance(value, springVelocity * dt);

		if (onUpdateValue) {
			onUpdateValue(value);
		}

		if (std::fabs(diff) < springEpsilon && std::fabs(springVelocity) < springEpsilon) {
			value = target;
			if (onUpdateValue) {
				onUpdateValue(value);
			}
			FinishAnimation();
		}
	}


	void FinishAnimation() {
		activeMode = Mode::None;
		springVelocity = 0.0f;

		if (onStop) {
			onStop(value);
		}
	}


	// ==================== Type operations ====================

	static T Lerp(const T& a, const T& b, float t) {
		return a + (b - a) * t;
	}

	static float Distance(const T& a, const T& b) {
		return std::fabs(ScalarDiff(b, a));
	}

	static float ScalarDiff(const T& a, const T& b) {
		if constexpr (std::is_arithmetic_v<T>) {
			return static_cast<float>(a) - static_cast<float>(b);
		} else {
			T diff = a - b;
			return diff.Magnitude();
		}
	}

	static T Advance(const T& v, float delta) {
		if constexpr (std::is_arithmetic_v<T>) {
			return static_cast<T>(static_cast<float>(v) + delta);
		} else {
			T direction = v.Normalized();
			return v + direction * delta;
		}
	}
};
