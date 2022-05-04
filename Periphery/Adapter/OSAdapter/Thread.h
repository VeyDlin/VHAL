#pragma once
#include "Port/Port.h"
#include <array>
#include <chrono>



namespace OSAdapter {
	constexpr tTaskEventMask defaultTaskMaskBits = 0b010101010;



	enum class StackDepth : std::uint16_t {
		minimal = 128U, medium = 256U, big = 512U, biggest = 1024U
	};



	class IThread {
	public:
		friend class RTOS;
		friend class RtosWrapper;


		virtual void Execute() = 0;


		static inline void Sleep(std::chrono::milliseconds timeOut = 1000ms) {
			RtosWrapper::wSleep(std::chrono::duration_cast<TicksPerSecond>(timeOut).count());
		}



		inline void SleepUntil(std::chrono::milliseconds timeOut = 1000ms) {
			RtosWrapper::wSleepUntil(lastWakeTime, std::chrono::duration_cast<TicksPerSecond>(timeOut).count());
		}



		inline void Signal(const tTaskEventMask mask = defaultTaskMaskBits) {
			RtosWrapper::wSignal(handle, mask);
		}



		inline tTaskEventMask WaitForSignal(std::chrono::milliseconds timeOut = 1000ms, const tTaskEventMask mask = defaultTaskMaskBits) {
			return RtosWrapper::wWaitForSignal(mask, std::chrono::duration_cast<TicksPerSecond>(timeOut).count());
		}



		inline void Resume() {
			RtosWrapper::wTaskResume(handle);
		}



		inline void Suspend() {
			RtosWrapper::wTaskSuspend(handle);
		}



	protected:
		tTaskContext context;
		tTaskHandle handle = nullptr;
		tTime lastWakeTime = 0U;

		inline virtual void Run() {
			lastWakeTime = RtosWrapper::wGetTicks();
			Execute();
		}
	};



	template<std::size_t stackSize>
	class ThreadStatic: public IThread {
		friend class RTOS;
		friend class RtosWrapper;

		static constexpr std::size_t stackDepth = stackSize;
		std::array<tStack, stackSize> stack;
	};





	template<std::size_t stackSize>
	class Thread: public IThread {
		friend class RTOS;

		static constexpr std::size_t stackDepth = stackSize;
	};
}
