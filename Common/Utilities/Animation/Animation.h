#pragma once
#include <functional>
#include <chrono>
#include <cmath>
#include <type_traits>
#include <Utilities/Math/IQMath/IQ.h>
#include "Easing.h"


template<RealType T>
class Animation {
public:
	enum class Mode { None, Tween, Spring };

	std::function<void(T)> onUpdateValue;
	std::function<void(T)> onStart;
	std::function<void(T)> onStop;


private:
	Mode activeMode = Mode::None;
	bool paused = false;

	T value{};
	T target{};

	T tweenFrom{};
	uint64 startTick = 0;
	uint64 durationMs = 0;

	typename Easing<T>::Curve easingType = Easing<T>::Curve::Linear;
	std::function<T(T)> easingFunction;

	T springStiffness = 200;
	T springDamping = 20;
	T springVelocity{};
	T springEpsilon = 0.001f;

	uint64 lastTick = 0;


public:
	// ==================== Tween ====================

	void Tween(T to, std::chrono::milliseconds duration) {
		TweenInternal(value, to, duration.count());
	}

	void Tween(T from, T to, std::chrono::milliseconds duration) {
		TweenInternal(from, to, duration.count());
	}

	void TweenSpeed(T to, T speed) {
		using std::fabs;
		T distance = fabs(to - value);
		uint64 ms = ToUint64(distance / speed * 1000);
		TweenInternal(value, to, ms);
	}

	void TweenSpeed(T from, T to, T speed) {
		using std::fabs;
		T distance = fabs(to - from);
		uint64 ms = ToUint64(distance / speed * 1000);
		TweenInternal(from, to, ms);
	}


	// ==================== Spring ====================

	void SetSpring(T stiffness, T damping) {
		springStiffness = stiffness;
		springDamping = damping;
	}

	void SetSpringEpsilon(T eps) {
		springEpsilon = eps;
	}

	void SetTarget(T newTarget) {
		target = newTarget;

		if (activeMode != Mode::Spring) {
			if (activeMode == Mode::None) {
				OnAnimationStart();
				if (onStart) {
					onStart(value);
				}
			}
			activeMode = Mode::Spring;
			springVelocity = 0;
		}
	}


	// ==================== Easing ====================

	void SetEasing(typename Easing<T>::Curve type) {
		easingType = type;
		easingFunction = nullptr;
	}

	void SetEasing(std::function<T(T)> fn) {
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
		springVelocity = 0;

		if (wasActive) {
			if (onStop) {
				onStop(value);
			}
			if (activeMode == Mode::None) {
				OnAnimationStop();
			}
		}
	}


	// ==================== Tick ====================

	void Update(uint64 nowMs) {
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
	virtual void OnAnimationStart() {}
	virtual void OnAnimationStop() {}

	static constexpr bool IsScalar() {
		return std::is_arithmetic_v<T> || IQType<T>;
	}


	static uint64 ToUint64(T v) {
		if constexpr (IQType<T>) {
			return static_cast<uint64>(v.ToInt());
		} else {
			return static_cast<uint64>(v);
		}
	}


	void TweenInternal(T from, T to, uint64 ms) {
		tweenFrom = from;
		target = to;
		value = from;
		durationMs = ms;
		startTick = 0;
		springVelocity = 0;

		if (activeMode == Mode::None) {
			OnAnimationStart();
			if (onStart) {
				onStart(from);
			}
		}

		activeMode = Mode::Tween;
	}


	void UpdateTween(uint64 nowMs) {
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

		uint64 elapsed = nowMs - startTick;

		T t = static_cast<int>(elapsed) / static_cast<int>(durationMs);

		if (t >= 1) {
			t = 1;
		}

		T easedT = easingFunction ? easingFunction(t) : Easing<T>::Apply(easingType, t);

		value = tweenFrom + (target - tweenFrom) * easedT;

		if (onUpdateValue) {
			onUpdateValue(value);
		}

		if (t >= 1) {
			FinishAnimation();
		}
	}


	void UpdateSpring(uint64 nowMs) {
		T dt;

		if (lastTick == 0 || lastTick >= nowMs) {
			dt = 0.001f;
		} else {
			dt = static_cast<int>(nowMs - lastTick) / 1000;
		}

		T maxDt = 0.1f;
		if (dt > maxDt) {
			dt = maxDt;
		}

		T diff = target - value;
		T force = diff * springStiffness;
		springVelocity = springVelocity + force * dt;
		springVelocity = springVelocity * (1 - springDamping * dt);

		value = value + springVelocity * dt;

		if (onUpdateValue) {
			onUpdateValue(value);
		}

		using std::fabs;
		if (fabs(diff) < springEpsilon && fabs(springVelocity) < springEpsilon) {
			value = target;
			if (onUpdateValue) {
				onUpdateValue(value);
			}
			FinishAnimation();
		}
	}


	void FinishAnimation() {
		activeMode = Mode::None;
		springVelocity = 0;

		if (onStop) {
			onStop(value);
		}
		if (activeMode == Mode::None) {
			OnAnimationStop();
		}
	}
};
