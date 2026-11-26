#pragma once
#include <functional>
#include <chrono>
#include <cmath>
#include <type_traits>
#include <Utilities/Math/IQMath/IQ.h>
#include <Utilities/Math/NumberRange.h>
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
	bool tweenStartPending = false;
	uint64 durationMs = 0;

	typename Easing<T>::Curve easingType = Easing<T>::Curve::Linear;
	std::function<T(T)> easingFunction;

	T springStiffness = T(200);
	T springDamping = T(20);
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
		uint64 ms = SpeedToDuration(distance, speed);
		TweenInternal(value, to, ms);
	}

	void TweenSpeed(T from, T to, T speed) {
		using std::fabs;
		T distance = fabs(to - from);
		uint64 ms = SpeedToDuration(distance, speed);
		TweenInternal(from, to, ms);
	}


	// ==================== Spring ====================

	void SetSpring(T stiffness, T damping) {
		CheckSpringRange();
		springStiffness = stiffness;
		springDamping = damping;
	}

	void SetSpringEpsilon(T eps) {
		springEpsilon = eps;
	}

	void SetTarget(T newTarget) {
		CheckSpringRange();
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


	// The spring's default stiffness is 200 and its force is the distance times the stiffness. A number type whose range is
	// below 200 would saturate the stiffness silently, so the spring entry points require this range. The force also saturates
	// once the distance exceeds the range divided by 200 (about 10 for IQ<20>). That limit depends on run-time values, so it is
	// documented, not checked. Tweens have no such limit.
	static void CheckSpringRange() {
		static_assert(RangeAtLeast<T, 200.0>, "The spring needs a number type whose range holds the stiffness 200. The force is 200 times the distance, so distances above range / 200 saturate. Use IQ with Q of 23 or less, or float.");
	}


	// The fraction elapsed / duration as T, for elapsed < duration and duration > 0. An IQ type divides the two integers in
	// 64-bit arithmetic, so the duration itself is never converted to T. A 1000 ms duration would saturate IQ<24>, whose
	// range is 128. Float divides the two converted operands.
	static T FractionOf(uint64 elapsed, uint64 duration) {
		if constexpr (IQType<T>) {
			return T::FromRaw(static_cast<int32>(FixedFraction(elapsed, duration, T::q)));
		} else {
			return static_cast<T>(elapsed) / static_cast<T>(duration);
		}
	}


	// floor(numerator * 2^bits / denominator) rounded to the nearest integer, ties up. Needs numerator < denominator < 2^63,
	// so the remainder shifted left stays in 64 bits. The result is at most 2^bits.
	static uint64 FixedFraction(uint64 numerator, uint64 denominator, int bits) {
		uint64 remainder = numerator;
		uint64 quotient = 0;
		for (int bit = 0; bit < bits; bit++) {
			remainder = remainder << 1;
			quotient = quotient << 1;
			if (remainder >= denominator) {
				remainder = remainder - denominator;
				quotient = quotient | 1u;
			}
		}
		if (remainder >= denominator - remainder) {
			quotient = quotient + 1;
		}
		return quotient;
	}


	// Duration of a constant-speed tween, in whole milliseconds, truncated. A speed or a distance that is not positive gives 0 ms,
	// so the tween ends at its target on the next update. An IQ divides the raw values in 64-bit integers. A float divides in T
	// and saturates a duration beyond the range of uint64, and NaN gives 0.
	static uint64 SpeedToDuration(T distance, T speed) {
		if constexpr (IQType<T>) {
			if (distance.raw <= 0 || speed.raw <= 0) {
				return 0;
			}
			return static_cast<uint64>(static_cast<int64>(distance.raw) * 1000 / speed.raw);
		} else {
			if (!(distance > T(0)) || !(speed > T(0))) {
				return 0;
			}
			T ms = distance / speed * T(1000);
			if (ms >= T(18446744073709551616.0)) {
				return ~static_cast<uint64>(0);
			}
			return static_cast<uint64>(ms);
		}
	}


	void TweenInternal(T from, T to, uint64 ms) {
		tweenFrom = from;
		target = to;
		value = from;
		durationMs = ms;
		tweenStartPending = true;
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
		// Latch the start on the first update after Tween(); a tick value of 0 is a valid start time.
		if (tweenStartPending) {
			startTick = nowMs;
			tweenStartPending = false;
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

		// At or past the end the fraction is 1. Below it, the fraction is computed from the two integers (see FractionOf).
		T t = elapsed >= durationMs ? T(1) : FractionOf(elapsed, durationMs);

		T easedT = easingFunction ? easingFunction(t) : Easing<T>::Apply(easingType, t);

		value = tweenFrom + (target - tweenFrom) * easedT;

		if (onUpdateValue) {
			onUpdateValue(value);
		}

		if (t >= 1) {
			FinishAnimation();
		}
	}


	// The spring step in seconds for the elapsed milliseconds, capped at 0.1 s. An IQ type takes the fraction of 1000 ms in
	// integers, because 1000 itself saturates Q22 and Q23.
	static T SpringStep(uint64 ms) {
		if (ms > 100) {
			ms = 100;
		}

		if constexpr (IQType<T>) {
			return FractionOf(ms, 1000);
		} else {
			return static_cast<T>(ms) / static_cast<T>(1000);
		}
	}


	void UpdateSpring(uint64 nowMs) {
		// The first tick and a clock that does not advance count as 1 ms.
		uint64 elapsedMs = 1;
		if (lastTick != 0 && lastTick < nowMs) {
			elapsedMs = nowMs - lastTick;
		}
		T dt = SpringStep(elapsedMs);

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
