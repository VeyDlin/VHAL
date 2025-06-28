#pragma once
#include <System/System.h>
#include <Adapter/OSAdapter/RTOS.h>
#include <Adapter/OSAdapter/Timer.h>
#include <Utilities/Data/Color/Colors.h>
#include <functional>


template<std::size_t stackSize>
class RgbAnimation {
public:
	Colors::FRgb valueStart;
	Colors::FRgb valueEnd;

	std::function<void(Colors::FRgb)> onUpdate;
	std::function<void(Colors::FRgb)> onStart;
	std::function<void(Colors::FRgb)> onStop;


private:
	std::chrono::milliseconds _duration = 1s;

	float steps;
	Colors::FRgb step;
	bool isActive = false;
	bool pause = false;


	class AnimationTimer: public Timer<stackSize> {
	public:
		RgbAnimation *a;
		virtual void Execute() override {
			if (!a->isActive || a->pause) {
				return;
			}

			a->steps--;
			if (a->steps >= 0) {
				a->valueStart.Add(a->step);
				if (a->onUpdate) {
					a->onUpdate(a->valueStart);
				}
			} else {
				a->valueStart = a->valueEnd;

				if (a->onUpdate) {
					a->onUpdate(a->valueEnd);
				}

				a->isActive = false;

				this->Stop();
				a->Stop();
			}

		}
	};

	AnimationTimer timer;


public:
	RgbAnimation(ThreadPriority prior = ThreadPriority::normal) {
		timer.interval = 17ms;
		timer.autoReload = true;
		timer.a = this;
		RTOS::CreateThread(timer, prior);
	}

	

	RgbAnimation& Animate(Colors::FRgb from, Colors::FRgb to, std::chrono::milliseconds duration) {
		if (isActive) {
			valueEnd = to;
			steps = std::max(1.0f, std::fabs(valueEnd - valueStart) / step);
			if (onStart) {
				onStart(from);
			}
			return *this;
		}

		pause = true;
		isActive = true;

		valueStart = from;
		valueEnd = to;
		_duration = duration;

		steps = std::max(1.0f,
		    static_cast<float>(_duration.count()) /
		    static_cast<float>(timer.interval.count()));

		step.r = (valueEnd.r - valueStart.r) / steps;
		step.g = (valueEnd.g - valueStart.g) / steps;
		step.b = (valueEnd.b - valueStart.b) / steps;

		if (onStart) {
			onStart(from);
		}

		pause = false;

		timer.Start();
		return *this;
	}


	RgbAnimation& Animate(Colors::FRgb to, std::chrono::milliseconds duration) {
		Animate(valueStart, to, duration);
		return *this;
	}


	RgbAnimation& Animate(Colors::FRgb to) {
		Animate(valueStart, to, _duration);
		return *this;
	}


	RgbAnimation& SetFps(uint8 val) {
		timer.interval = 1000ms / val;
		return *this;
	}


	LinearAnimation& SetInterval(std::chrono::milliseconds interval) {
		timer.interval = interval;
		return *this;
	}


	std::chrono::milliseconds GetInterval() {
		return timer.interval;
	}


	RgbAnimation& SetDuration(std::chrono::milliseconds val) {
		_duration = val;
		return *this;
	}


	std::chrono::milliseconds GetDuration() {
		return _duration;
	}


	RgbAnimation& Pause() {
		pause = true;
		return *this;
	}


	RgbAnimation& Stop() {
		isActive = false;
		pause = false;
		if (onStop) {
			onStop(valueEnd);
		}
		return *this;
	}


	RgbAnimation& WaitEnd() {
		while(isActive);
		return *this;
	}


	bool IsActive() const {
		return isActive;
	}


	Colors::FRgb GetEndValue() const {
		return valueEnd;
	}
};
