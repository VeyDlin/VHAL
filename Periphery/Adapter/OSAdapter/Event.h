#pragma once
#include "Port/Port.h"
#include "EventMode.h"


namespace OSAdapter {
	class Event {
	public:
		static constexpr tEventBits defaultMask = { 0b11111111 };

	public:
		inline Event(const std::chrono::milliseconds delay, const tEventBits maskBits) :
			timeOut {
				(std::chrono::duration_cast<TicksPerSecond>(delay)).count()
			}, mask {
				maskBits
			}
		{
			handle = RtosWrapper::wCreateEvent(event);
		}

		inline ~Event() {
			RtosWrapper::wDeleteEvent(handle);
		}

		inline void Signal() {
			RtosWrapper::wSignalEvent(handle, mask);
		}

		inline tEventBits Wait(const EventMode mode = EventMode::waitAnyBits, const tEventBits maskBits = defaultMask) const {
			return RtosWrapper::wWaitEvent(handle, maskBits, timeOut, mode);
		}

		inline void SetTimeout(std::chrono::milliseconds delay) {
			timeOut = (std::chrono::duration_cast<TicksPerSecond>(delay)).count();
		}

		inline void SetMaskBits(tEventBits maskBits) {
			mask = maskBits;
		}

	private:
		tEventHandle handle { 0 };
		tEvent event;
		tTime timeOut = 1000;
		tEventBits mask { defaultMask };
	};
}