#pragma once
#include <Utilities/Data/Colors.h>
#include "Animation.h"


template<RealType T = float, typename AnimationType = Animation<T>>
class RgbAnimation {
public:
	std::function<void(Colors::IRgb<T>)> onUpdateValue;
	std::function<void(Colors::IRgb<T>)> onStart;
	std::function<void(Colors::IRgb<T>)> onStop;


private:
	AnimationType progress;
	Colors::IRgb<T> from{};
	Colors::IRgb<T> to{};


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

	void Tween(Colors::IRgb<T> target, std::chrono::milliseconds duration) {
		from = Lerp(from, to, progress.GetValue());
		to = target;
		progress.Tween(0, 1, duration);
	}

	void Tween(Colors::IRgb<T> start, Colors::IRgb<T> target, std::chrono::milliseconds duration) {
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

	void SetTarget(Colors::IRgb<T> target) {
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

	Colors::IRgb<T> GetValue() const {
		return Lerp(from, to, progress.GetValue());
	}

	Colors::IRgb<T> GetEndValue() const {
		return to;
	}

	void SetValue(Colors::IRgb<T> val) {
		from = val;
		to = val;
		progress.SetValue(0);
	}


private:
	static Colors::IRgb<T> Lerp(const Colors::IRgb<T>& a, const Colors::IRgb<T>& b, T t) {
		return {
			a.r + (b.r - a.r) * t,
			a.g + (b.g - a.g) * t,
			a.b + (b.b - a.b) * t
		};
	}
};
