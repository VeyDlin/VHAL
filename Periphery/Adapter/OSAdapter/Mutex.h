#pragma once
#include "Port/Port.h"


namespace OSAdapter {
	class Mutex {
	public:
		static constexpr std::chrono::milliseconds waitForEver = RtosWrapper::waitForEver;
		static constexpr std::chrono::milliseconds notWait = RtosWrapper::notWait;

		inline Mutex() : handle(RtosWrapper::wCreateMutex(mutex)) { }

		inline ~Mutex() {
			RtosWrapper::wDeleteMutex(handle);
		}

		inline bool Lock(const std::chrono::milliseconds timeOut = notWait) {
			return RtosWrapper::wLockMutex(handle, std::chrono::duration_cast<TicksPerSecond>(timeOut).count());
		}

		inline void UnLock() {
			RtosWrapper::wUnLockMutex(handle);
		}

	private:
		tMutex mutex;
		tMutexHandle handle;
	};
}