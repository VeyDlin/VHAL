#pragma once
#include "Port/Port.h"
#include "Thread.h"
#include "Timer.h"
#include <cstddef>



namespace OSAdapter {
	class RTOS {
	public:
		template<std::size_t stackSize>
		static inline void CreateThread(ThreadStatic<stackSize> &thread, ThreadPriority prior = ThreadPriority::normal, const char *pName = NULL) {
			RtosWrapper::wCreateThreadStatic<RTOS>(thread, pName, prior, thread.stackDepth, thread.stack.data());
		}


		template<std::size_t stackSize>
		static inline void CreateThread(Thread<stackSize> &thread, ThreadPriority prior = ThreadPriority::normal, const char *pName = NULL) {
			RtosWrapper::wCreateThread<RTOS>(thread, pName, prior, thread.stackDepth);
		}


		static inline void Sleep(std::chrono::milliseconds timeOut) {
			RtosWrapper::wSleep(std::chrono::duration_cast<TicksPerSecond>(timeOut).count());
		}


		static inline void Start() {
			RtosWrapper::wStart();
		}


		static inline bool IsSchedulerRun() {
			return RtosWrapper::wIsSchedulerRun();
		}


		static inline void HandleSvcInterrupt() {
			RtosWrapper::wHandleSvcInterrupt();
		}


		static inline void HandleSysTickInterrupt() {
			RtosWrapper::wHandleSysTickInterrupt();
		}


		friend class RtosWrapper;

	private:
		static inline void Run(void *pContext) {
			static_cast<IThread*>(pContext)->Run();
		}
	};
}

