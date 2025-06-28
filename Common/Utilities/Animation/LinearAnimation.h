#pragma once
#include <System/System.h>
#include <Adapter/OSAdapter/RTOS.h>
#include <Adapter/OSAdapter/Timer.h>
#include <functional>


template<std::size_t stackSize>
class LinearAnimation {
public:
	float valueStart = 0;
	float valueEnd = 0;

	std::function<void(float)> onUpdateValue;
	std::function<void(float)> onStart;
	std::function<void(float)> onStop;


private:
	std::chrono::milliseconds _duration = 1s;

	float steps;
	float step;
	bool isActive = false;
	bool pause = false;


	class AnimationTimer: public Timer<stackSize> {
	public:
		LinearAnimation *a;
		virtual void Execute() override {
			if (!a->isActive || a->pause) {
				return;
			}

			a->steps--;
			if (a->steps >= 0) {
				a->valueStart += a->step;
				if (a->onUpdateValue) {
					a->onUpdateValue(a->valueStart);
				}
			} else {
				a->valueStart = a->valueEnd;

				if (a->onUpdateValue) {
					a->onUpdateValue(a->valueEnd);
				}

				a->isActive = false;

				this->Stop();
				a->Stop();
			}
		}
	};

	AnimationTimer timer;


public:
	LinearAnimation(ThreadPriority prior = ThreadPriority::normal) {
		timer.interval = 17ms;
		timer.autoReload = true;
		timer.a = this;
		RTOS::CreateThread(timer, prior);
	}


	LinearAnimation& Animate(float from, float to, std::chrono::milliseconds duration) {
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

		step = (valueEnd - valueStart) / steps;

		if (onStart) {
			onStart(from);
		}

		pause = false;

		timer.Start();
		return *this;
	}


	LinearAnimation& Animate(float to, std::chrono::milliseconds duration) {
		Animate(valueStart, to, duration);
		return *this;
	}


	LinearAnimation& Animate(float to) {
		Animate(valueStart, to, _duration);
		return *this;
	}


	LinearAnimation& SetFps(uint8 val) {
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


	LinearAnimation& SetDuration(std::chrono::milliseconds val) {
		_duration = val;
		return *this;
	}


	std::chrono::milliseconds GetDuration() {
		return _duration;
	}


	LinearAnimation& Pause() {
		pause = true;
		return *this;
	}


	LinearAnimation& Stop() {
		isActive = false;
		pause = false;
		if (onStop) {
			onStop(valueEnd);
		}
		return *this;
	}


	LinearAnimation& WaitEnd() {
		while(isActive);
		return *this;
	}


	bool IsActive() const {
		return isActive;
	}


	float GetEndValue() const {
		return valueEnd;
	}
};
