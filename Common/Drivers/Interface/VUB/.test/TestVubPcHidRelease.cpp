#include "SimCheck.h"
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <ntdef.h>
#include <hidusage.h>
extern "C" {
#include <hidpi.h>
#include <hidsdi.h>
}
#include <setupapi.h>
#include <vector>

// The link's private state is reached here to make a write pending without a device. Only the Windows test needs it.
#define private public
#include <Drivers/Interface/VUB/PC/HidLink.h>
#undef private
#else
#include <Drivers/Interface/VUB/PC/HidLink.h>
#endif


// These checks use no device. A stand-in handle (NUL) and an overlapped write marked as in flight stand for a write the OS has not
// completed. The test completes it by hand, by clearing the overlapped status and setting its event.
#if defined(_WIN32)
using Hid = VUB::PC::HidLink<64>;

// STATUS_PENDING: the status an overlapped operation has while it is in flight.
constexpr ULONG_PTR statusPending = 0x103;


// Makes the link look open, with one write in flight on its event.
inline void MarkWriteInFlight(Hid& link) {
	link.portOpen = true;
	link.device = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
	link.writeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	link.writeOverlapped = OVERLAPPED{};
	link.writeOverlapped.hEvent = link.writeEvent;
	link.writeOverlapped.Internal = statusPending;
	link.writePending = true;
}


// Completes the write the test marked in flight.
inline void CompleteWrite(Hid& link) {
	link.writeOverlapped.Internal = 0;
	SetEvent(link.writeEvent);
}


// A Send while the previous report is on its way is busy, and the report on its way is left as it was: the packet is not packed over it.
inline void PendingWriteIsNotOverwritten() {
	Hid link;
	MarkWriteInFlight(link);
	link.writeBuffer.fill(0xAA);
	const std::array<std::uint8_t, 3> packet = {1, 2, 3};
	SIM_EXPECT(link.Send(packet) == ResultStatus::busy);
	bool untouched = true;
	for (std::uint8_t value : link.writeBuffer) {
		untouched = untouched && value == 0xAA;
	}

	SIM_EXPECT(untouched);
	CompleteWrite(link);
}


// A write that never completes: Close waits about 200 ms, reports the link closed, and leaves the release in progress with the handles kept.
// Once the write completes, the release finishes and frees them.
inline void CloseWaitsBoundedAndKeepsHandles() {
	Hid link;
	MarkWriteInFlight(link);
	std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	link.Close();
	std::chrono::milliseconds spent = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
	SIM_EXPECT(!link.IsOpen());
	SIM_EXPECT(spent.count() >= 150);
	SIM_EXPECT(spent.count() < 2000);
	SIM_EXPECT(link.release.Closing());
	SIM_EXPECT(link.device != INVALID_HANDLE_VALUE);

	CompleteWrite(link);
	Hid::ReleaseLayer layer(link);
	SIM_EXPECT(link.release.Finish(layer, VUB::PC::ReleaseWait::None));
	SIM_EXPECT(!link.release.Closing());
	SIM_EXPECT(link.device == INVALID_HANDLE_VALUE);
}
#endif


int main() {
#if defined(_WIN32)
	PendingWriteIsNotOverwritten();
	CloseWaitsBoundedAndKeepsHandles();
#endif
	return SimFinish("TestVubPcHidRelease");
}
