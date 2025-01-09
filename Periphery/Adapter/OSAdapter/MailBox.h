#pragma once
#include "Port/Port.h"
#include <array>


namespace OSAdapter {
	template<typename T, std::size_t size = 1>
	class MailBox {
	public:
		static constexpr std::chrono::milliseconds waitForEver = RtosWrapper::waitForEver;
		static constexpr std::chrono::milliseconds notWait = RtosWrapper::notWait;

		inline MailBox() {
			handle = RtosWrapper::wMailBoxCreate(size, sizeof(T), buffer.data(), context);
		}

		inline ~MailBox() {
			RtosWrapper::wMailBoxDelete(handle);
		}

		inline bool Put(T &item, const std::chrono::milliseconds timeOut = 0ms) { // TODO: notWait not working
			return RtosWrapper::wMailBoxPut(handle, &item, std::chrono::duration_cast<TicksPerSecond>(timeOut).count());
		}

		inline bool Get(T &item, const std::chrono::milliseconds timeOut = 0ms) { // TODO: notWait not working
			return RtosWrapper::wMailBoxGet(handle, &item, std::chrono::duration_cast<TicksPerSecond>(timeOut).count());
		}

	private:
		tMailBoxHandle handle;
		tMailBoxContext context;
		std::array<std::uint8_t, size * sizeof(T)> buffer;
	};
}