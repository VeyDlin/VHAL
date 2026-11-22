#pragma once
#include "IAdapter.h"

#define VHAL_RTC_ADAPTER


// Real-time clock: calendar, alarms, periodic wakeup and backup registers, kept while the core sleeps
// (STM32 RTC, ESP32 RTC timer, nRF RTC with the calendar kept by the port).
//
// The clock source (32.768 kHz crystal or the internal low-speed oscillator) is chosen and started by the BSP, like
// every other clock; the constructor takes its frequency so the port can set its prescalers. Backup-domain write
// protection, where the hardware has it, is handled inside the port.
//
// Interrupts: IrqHandler() serves the alarm and wakeup interrupts; the application calls it from each RTC vector it
// enables (on STM32 the alarm and wakeup lines are separate vectors).
template<typename HandleType>
class RTCAdapter : public IAdapter {
public:
	enum class Weekday : uint8 {
		Monday = 1,
		Tuesday,
		Wednesday,
		Thursday,
		Friday,
		Saturday,
		Sunday
	};

	struct DateTime {
		// Full year, 2000 to 2099 on ports whose calendar keeps two digits.
		uint16 year = 2000;
		// 1 to 12.
		uint8 month = 1;
		// 1 to 31.
		uint8 day = 1;
		// Derived from the date by SetDateTime(); ignored when written.
		Weekday weekday = Weekday::Saturday;
		uint8 hours = 0;
		uint8 minutes = 0;
		uint8 seconds = 0;
		// Fraction of the current second, 0 to 999999. Read only: SetDateTime() starts the second at 0.
		uint32 microseconds = 0;
	};

	// Which fields of an alarm must match. A field that is not compared matches every value.
	struct AlarmMatch {
		bool seconds = true;
		bool minutes = true;
		bool hours = true;
		// Compares the day of the month, or the weekday when byWeekday is set.
		bool day = true;
		bool byWeekday = false;
	};

	struct Alarm {
		DateTime time;
		AlarmMatch match;
	};

	struct Parameters {
		// Calibration in parts per billion of the clock source, positive to speed the clock up. Ports without smooth
		// calibration accept only 0.
		int32 calibrationPpb = 0;
	};

	std::function<void(uint8 alarmIndex)> onAlarm;
	std::function<void()> onWakeup;


protected:
	HandleType *rtcHandle = nullptr;
	uint32 inputClockHz = 0;
	Parameters parameters;


public:
	RTCAdapter() = default;


	RTCAdapter(HandleType *rtc, uint32 clockSourceHz) : rtcHandle(rtc), inputClockHz(clockSourceHz) {
	}


	// Replaces onAlarm while its interrupt may run. See "Changing a callback while its interrupt may run" in the wiki.
	void SetOnAlarm(std::function<void(uint8 alarmIndex)> handler) {
		std::function<void(uint8 alarmIndex)> old;
		System::CriticalSection(true);
		old = std::move(onAlarm);
		onAlarm = std::move(handler);
		System::CriticalSection(false);
	}


	// Replaces onWakeup while its interrupt may run. See "Changing a callback while its interrupt may run" in the wiki.
	void SetOnWakeup(std::function<void()> handler) {
		std::function<void()> old;
		System::CriticalSection(true);
		old = std::move(onWakeup);
		onWakeup = std::move(handler);
		System::CriticalSection(false);
	}


	// Sets the prescalers for the clock source and the calibration. The calendar keeps running and is not reset, so this
	// is safe after a reset or a wakeup from standby.
	virtual ResultStatus SetParameters(Parameters val) {
		parameters = val;
		return Initialization();
	}


	const Parameters& GetParameters() const {
		return parameters;
	}


	// True when the calendar has been set since the backup domain lost power.
	virtual bool IsTimeSet() const = 0;


	// invalidParameter for a date that does not exist or a year outside the port's range.
	virtual ResultStatus SetDateTime(const DateTime& dateTime) = 0;


	// notReady before the calendar was set (IsTimeSet() false).
	virtual Result<DateTime> GetDateTime() const = 0;


	// Number of alarms the hardware has.
	virtual uint8 GetAlarmCount() const = 0;


	// Arms alarm index; it fires onAlarm(index) on every match until DisableAlarm(). outOfRange for an index beyond
	// GetAlarmCount().
	virtual ResultStatus SetAlarm(uint8 index, const Alarm& alarm) = 0;


	virtual ResultStatus DisableAlarm(uint8 index) = 0;


	// Fires onWakeup every periodMs, also from the low-power modes that keep the RTC running. invalidParameter for a
	// period the port cannot make (0, or beyond its counter).
	virtual ResultStatus SetWakeupTimer(uint32 periodMs) = 0;


	virtual ResultStatus DisableWakeupTimer() = 0;


	// Number of 32-bit backup registers kept while the backup domain has power.
	virtual uint8 GetBackupRegisterCount() const = 0;


	virtual ResultStatus WriteBackupRegister(uint8 index, uint32 value) = 0;


	virtual Result<uint32> ReadBackupRegister(uint8 index) const = 0;


	virtual void IrqHandler() = 0;


	// Day of the week of a date in the Gregorian calendar (Sakamoto's method), for ports and applications.
	static constexpr Weekday CalculateWeekday(uint16 year, uint8 month, uint8 day) {
		constexpr uint8 offsets[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
		uint16 y = year;
		if (month < 3) {
			y = static_cast<uint16>(y - 1);
		}

		uint16 sundayBased = static_cast<uint16>((y + y / 4 - y / 100 + y / 400 + offsets[month - 1] + day) % 7);
		if (sundayBased == 0) {
			return Weekday::Sunday;
		}

		return static_cast<Weekday>(sundayBased);
	}


	static constexpr bool IsLeapYear(uint16 year) {
		return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
	}


	static constexpr uint8 DaysInMonth(uint16 year, uint8 month) {
		constexpr uint8 days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
		if (month == 2 && IsLeapYear(year)) {
			return 29;
		}

		return days[month - 1];
	}


protected:
	// Prescalers from inputClockHz (1 Hz calendar tick) and the calibration. Calls BeforeInitialization() and
	// AfterInitialization().
	virtual ResultStatus Initialization() = 0;


	virtual inline void CallAlarm(uint8 alarmIndex) {
		if (onAlarm != nullptr) {
			onAlarm(alarmIndex);
		}
	}


	virtual inline void CallWakeup() {
		if (onWakeup != nullptr) {
			onWakeup();
		}
	}
};
