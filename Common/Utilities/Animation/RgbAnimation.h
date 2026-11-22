#pragma once
#include <Utilities/Data/Colors/Colors.h>
#include <Utilities/Math/IQMath/IQ.h>
#include "Animation.h"


// The colours stay float (Colors::FRgb). The progress and the blend run in T, so a fixed-point type like IQ<24> works too.
template<RealType T = float, typename AnimationType = Animation<T>>
class RgbAnimation {
public:
	std::function<void(Colors::FRgb)> onUpdateValue;
	std::function<void(Colors::FRgb)> onStart;
	std::function<void(Colors::FRgb)> onStop;


private:
	AnimationType progress;
	Colors::FRgb from{};
	Colors::FRgb to{};


public:
	RgbAnimation() {
		progress.onUpdateValue = [this](T t) {
			if (onUpdateValue) {
				onUpdateValue(Lerp(from, to, t));
			}
		};

		progress.onStart = [this](T) {
			if (onStart) {
				onStart(Lerp(from, to, progress.GetValue()));
			}
		};

		progress.onStop = [this](T) {
			if (onStop) {
				onStop(to);
			}
		};
	}


	AnimationType& GetProgress() {
		return progress;
	}


	// ==================== Tween ====================

	void Tween(Colors::FRgb target, std::chrono::milliseconds duration) {
		from = Lerp(from, to, progress.GetValue());
		to = target;
		progress.Tween(0, 1, duration);
	}

	void Tween(Colors::FRgb start, Colors::FRgb target, std::chrono::milliseconds duration) {
		from = start;
		to = target;
		progress.Tween(0, 1, duration);
	}


	// ==================== Spring ====================

	void SetSpring(T stiffness, T damping) {
		progress.SetSpring(stiffness, damping);
	}

	void SetSpringEpsilon(T eps) {
		progress.SetSpringEpsilon(eps);
	}

	void SetTarget(Colors::FRgb target) {
		from = Lerp(from, to, progress.GetValue());
		to = target;
		progress.SetValue(0);
		progress.SetTarget(1);
	}


	// ==================== Easing ====================

	void SetEasing(typename Easing<T>::Curve type) {
		progress.SetEasing(type);
	}

	void SetEasing(std::function<T(T)> fn) {
		progress.SetEasing(fn);
	}


	// ==================== Control ====================

	void Pause() {
		progress.Pause();
	}

	void Resume() {
		progress.Resume();
	}

	void Stop() {
		progress.Stop();
	}


	// ==================== Tick ====================

	void Update(uint64 nowMs) {
		progress.Update(nowMs);
	}


	// ==================== State ====================

	bool IsActive() const {
		return progress.IsActive();
	}

	Colors::FRgb GetValue() const {
		return Lerp(from, to, progress.GetValue());
	}

	Colors::FRgb GetEndValue() const {
		return to;
	}

	void SetValue(Colors::FRgb val) {
		from = val;
		to = val;
		progress.SetValue(0);
	}


private:
	// The channels are converted to T first: a float times an IQ does not compile. For float T the conversions are no-ops.
	static Colors::FRgb Lerp(const Colors::FRgb& a, const Colors::FRgb& b, T t) {
		return {
			static_cast<float>(T(a.r) + (T(b.r) - T(a.r)) * t),
			static_cast<float>(T(a.g) + (T(b.g) - T(a.g)) * t),
			static_cast<float>(T(a.b) + (T(b.b) - T(a.b)) * t)
		};
	}
};
