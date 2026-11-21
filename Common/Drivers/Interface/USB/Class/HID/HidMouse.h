#pragma once
#include <Drivers/Interface/USB/Class/HID/HidFunction.h>
#include <System/System.h>
#include <algorithm>
#include <array>
#include <cstddef>


// Mouse on a boot mouse interface (USBHidReportBuilder::BootMouse()): buttons, X, Y and the wheel, four bytes per report. Each report
// carries the current buttons and at most 127 steps per axis, so a larger movement is split into several reports, one per queue place.
//
// Movement is accumulated. Move() and Scroll() add to the pending movement and send as many reports as the queue has room for. The rest
// stays pending while the queue is full, and the call returns busy. Flush() sends it later, from the application's loop, once the host
// has taken reports. Click() and SetButtons() also send the pending movement first.
//
// The pending movement of each axis is bounded to MaxPendingSteps (the int16 range). A Move() or Scroll() that would take an axis past the
// bound is refused with outOfRange and changes nothing: the host has to take the pending reports first. busy means the movement was
// taken and waits in the mouse for queue room; outOfRange means it was not taken at all. A refused call leaves the buttons and the
// pending movement as they were, except that SetButtons() keeps the new buttons when its report is refused, so the next report carries them.
//
// The pending movement is dropped when the function is deconfigured or the bus resets (HidFunction::ResetDelegate). A host that enumerates
// the device again would otherwise receive the movement made while it was away as one jump. The buttons are kept: the application sets them.
//
// The mouse registers itself as the function's reset delegate, so the reset reaches the mouse and not a delegate of the application. Like
// HidKeyboard, create the mouse with the function and before the device starts, and keep it alive for as long as the function is.
//
// The methods run in thread context and take a critical section around the pending movement, since a reset from the interrupt changes it.
template<std::size_t QueueDepth>
class HidMouse {
public:
	static constexpr uint8 ButtonLeft = 0x01;
	static constexpr uint8 ButtonRight = 0x02;
	static constexpr uint8 ButtonMiddle = 0x04;
	// The largest pending movement of one axis, in steps: the int16 range.
	static constexpr int32 MaxPendingSteps = 32767;


private:
	static constexpr std::size_t ReportLength = 4;
	static constexpr int32 StepLimit = 127;
	static constexpr uint8 ButtonMask = 0x07;


	HidFunction<QueueDepth>& hid;
	uint8 buttons = 0;
	int32 pendingX = 0;
	int32 pendingY = 0;
	int32 pendingWheel = 0;


public:
	explicit HidMouse(HidFunction<QueueDepth>& function)
		: hid(function) {
		hid.SetResetDelegate(HidFunction<QueueDepth>::ResetDelegate::template Bind<HidMouse, &HidMouse::OnFunctionReset>(this));
	}


	// Moves by dx and dy counts (positive right and down). Sends what the queue has room for. outOfRange, with nothing changed, when an axis
	// would pass MaxPendingSteps.
	ResultStatus Move(int32 dx, int32 dy) {
		System::CriticalSection(true);
		ResultStatus status = ResultStatus::outOfRange;
		if (Fits(pendingX, dx) && Fits(pendingY, dy)) {
			pendingX += dx;
			pendingY += dy;
			status = Emit(false);
		}

		System::CriticalSection(false);
		return status;
	}


	// Wheel steps, positive away from the user. outOfRange, with nothing changed, when the wheel would pass MaxPendingSteps.
	ResultStatus Scroll(int32 wheel) {
		System::CriticalSection(true);
		ResultStatus status = ResultStatus::outOfRange;
		if (Fits(pendingWheel, wheel)) {
			pendingWheel += wheel;
			status = Emit(false);
		}

		System::CriticalSection(false);
		return status;
	}


	// Sends the pending movement that the queue has room for.
	ResultStatus Flush() {
		System::CriticalSection(true);
		ResultStatus status = Emit(false);
		System::CriticalSection(false);
		return status;
	}


	// Sets the buttons (ButtonLeft, ButtonRight, ButtonMiddle as bits) and sends one report at least.
	ResultStatus SetButtons(uint8 mask) {
		System::CriticalSection(true);
		buttons = static_cast<uint8>(mask & ButtonMask);
		ResultStatus status = Emit(true);
		System::CriticalSection(false);
		return status;
	}


	// Presses and releases one button: two reports. The pending movement goes first. The places for the movement and for the two reports
	// are checked before anything is sent, so a refused click sends nothing and leaves the buttons as they were.
	ResultStatus Click(uint8 button) {
		System::CriticalSection(true);
		ResultStatus status = ClickUnderSection(button);
		System::CriticalSection(false);
		return status;
	}


	uint8 Buttons() const {
		return buttons;
	}


private:
	// The caller holds the critical section.
	ResultStatus ClickUnderSection(uint8 button) {
		if (hid.FreeSlots() < PendingFrames() + 2) {
			return ResultStatus::busy;
		}

		ResultStatus status = Emit(false);
		if (status != ResultStatus::ok) {
			return status;
		}

		uint8 previous = buttons;
		buttons = static_cast<uint8>((previous | button) & ButtonMask);
		status = SendFrame(0, 0, 0);
		if (status != ResultStatus::ok) {
			buttons = previous;
			return status;
		}

		buttons = previous;
		return SendFrame(0, 0, 0);
	}


	// Whether the pending total of one axis stays within MaxPendingSteps after adding delta. The sum is taken in 64 bits, so no input overflows.
	static bool Fits(int32 pending, int32 delta) {
		int64 total = static_cast<int64>(pending) + static_cast<int64>(delta);
		return total >= -MaxPendingSteps && total <= MaxPendingSteps;
	}


	// The reset of the function: the host has lost the movement the mouse was still sending, so the pending total is dropped. Runs in the
	// context of the event (see HidFunction::ResetDelegate).
	void OnFunctionReset() {
		pendingX = 0;
		pendingY = 0;
		pendingWheel = 0;
	}


	static int8 TakeStep(int32 value) {
		if (value > StepLimit) {
			return static_cast<int8>(StepLimit);
		}

		if (value < -StepLimit) {
			return static_cast<int8>(-StepLimit);
		}

		return static_cast<int8>(value);
	}


	// The steady copy has the same buttons and no motion. The idle repeat and GET_REPORT of the function answer with it, so a repeat
	// does not move the pointer again.
	ResultStatus SendFrame(int8 x, int8 y, int8 wheel) {
		std::array<uint8, ReportLength> report{};
		report[0] = buttons;
		report[1] = static_cast<uint8>(x);
		report[2] = static_cast<uint8>(y);
		report[3] = static_cast<uint8>(wheel);
		std::array<uint8, ReportLength> steady{};
		steady[0] = buttons;
		return hid.SendReport(report, steady);
	}


	// The reports the pending movement takes: one per 127 steps of the largest axis.
	std::size_t PendingFrames() const {
		return std::max({ FramesFor(pendingX), FramesFor(pendingY), FramesFor(pendingWheel) });
	}


	static std::size_t FramesFor(int32 value) {
		int32 magnitude = value < 0 ? -value : value;
		return static_cast<std::size_t>((magnitude + StepLimit - 1) / StepLimit);
	}


	// Sends reports until the pending movement is gone. force sends one report even with no movement, for a change of buttons.
	// Each report takes at most 127 steps per axis and leaves the rest pending, so a refused report loses nothing. The caller holds the section.
	ResultStatus Emit(bool force) {
		bool first = true;
		while ((force && first) || pendingX != 0 || pendingY != 0 || pendingWheel != 0) {
			if (hid.FreeSlots() == 0) {
				return ResultStatus::busy;
			}

			int8 stepX = TakeStep(pendingX);
			int8 stepY = TakeStep(pendingY);
			int8 stepWheel = TakeStep(pendingWheel);
			ResultStatus status = SendFrame(stepX, stepY, stepWheel);
			if (status != ResultStatus::ok) {
				return status;
			}

			pendingX -= stepX;
			pendingY -= stepY;
			pendingWheel -= stepWheel;
			first = false;
		}

		return ResultStatus::ok;
	}
};
