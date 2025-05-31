#pragma once
#include "Thread.h"
#include "RTOS.h"
#include <functional>


namespace OSAdapter {
	template<class baseClass>
	class ITimer: public baseClass {
	public:
		enum class State : uint8 {
			Run, Stoped, Restart
		};

		std::chrono::milliseconds interval = 1s;
		bool autoReload = false;


	private:
		tTime sleepOutTime;
		State state = State::Stoped;


	public:
		virtual void Execute() = 0;


		virtual void Start() {
			if(state == State::Stoped) {
				UpdateSleepOutTime();
				state = State::Run;
				this->Resume();
			}
		}


		virtual void Stop() {
			state = State::Stoped;
		}


		virtual void Restart() {
			if(state == State::Run) {
				state = State::Restart;
			}
		}


	protected:
		inline void Run() override {
			this->lastWakeTime = RtosWrapper::wGetTicks();
			while(true) {

				if(state == State::Run) {
					UpdateSleepOutTime();
					this->Sleep(interval);

					auto time = RtosWrapper::wGetTicks();
					if(time < sleepOutTime && state == State::Run) {
						auto addTime = static_cast<TicksPerSecond>(sleepOutTime - time);
						auto addTameMs = std::chrono::duration_cast<std::chrono::milliseconds>(addTime);
						this->Sleep(addTameMs);
					}
				}

				switch (state) {
					case State::Run:
						Execute();
						if(autoReload == false) {
							Stop();
						}
					break;

					case State::Stoped:
						this->Suspend();
					break;

					case State::Restart:
						state = State::Run;
					break;
				}

			}
		}


		inline void UpdateSleepOutTime() {
			sleepOutTime = RtosWrapper::wGetTicks() + std::chrono::duration_cast<TicksPerSecond>(interval).count();
		}
	};


	template<class baseClass>
	class ITimerHandle: public baseClass {
	public:
		std::function<void()> onTick;

		virtual void Execute() override {
			if(onTick) {
				onTick();
			}
		}
	};


	template<std::size_t stackSize>
	class TimerStatic: public ITimer<ThreadStatic<stackSize>> { };


	template<std::size_t stackSize>
	class Timer: public ITimer<Thread<stackSize>> { };


	template<std::size_t stackSize>
	class TimerHandleStatic: public ITimerHandle<TimerStatic<stackSize>> { };


	template<std::size_t stackSize>
	class TimerHandle: public ITimerHandle<Timer<stackSize>> { };
}
