#pragma once
#include <Adapter/RTCAdapter.h>


// RTC port for STM32F4 (RTC v2: RM0090 for F405/F407/F427/F429, RM0383 for F411).
//
// BSP must:
//  - Select the RTC clock source in RCC_BDCR (LSE 32.768 kHz or LSI) and enable it (RTCEN). The constructor takes that
//    source's frequency. The port returns notReady while no source is selected or RTCEN is clear.
//  - Enable the PWR peripheral clock (LL_APB1_GRP1_PERIPH_PWR). The port sets PWR_CR.DBP around each call that writes the
//    RTC or the backup registers and restores it afterwards, so the backup domain is not left writable.
//  - Configure EXTI line 17 (RTC alarm A and B) and EXTI line 22 (RTC wakeup) as rising-edge triggers, unmasked in
//    EXTI_IMR. The RTC flags stay set until cleared; the NVIC only sees the EXTI edge.
//  - Enable NVIC RTC_Alarm_IRQn (41, EXTI 17) and RTC_WKUP_IRQn (3, EXTI 22). Both vectors call IrqHandler(); the
//    handler checks which flag is set, so one shared call per vector is enough.
//  - Not reset the backup domain between calls, or the calendar and backup registers are lost.
//
// Notes on this port:
//  - The alarm resolution is one second; the sub-second part of an alarm is not compared.
//  - The wakeup timer uses RTC/2 to RTC/16 while the period fits 16 bits, then ck_spre (1 Hz) with the 2^16 extension
//    for periods up to 131072 s.
//  - Smooth calibration uses the 32 s window, so the step is about 0.95 ppm and the range is about -487 ppm to +488 ppm.


using ARTC = class RTCAdapterF4;


class RTCAdapterF4: public RTCAdapter<RTC_TypeDef> {
private:
	using FlagReader = uint32 (*)(RTC_TypeDef *);


	// RTC_BKP0R to RTC_BKP19R on F4.
	static constexpr uint8 BackupRegisterCount = 20;
	static constexpr uint8 AlarmCount = 2;
	// Upper bound for a flag wait. The flags settle within a couple of RTC clock cycles (about 60 us at 32.768 kHz), so
	// this is only a guard against a stopped clock, not a timing budget.
	static constexpr uint32 FlagWaitLoops = 100000;
	// PREDIV_A is 7 bits and PREDIV_S is 15 bits; both registers hold the divider minus one.
	static constexpr uint32 AsyncPredivMax = 128;
	static constexpr uint32 SyncPredivMax = 0x7FFF;
	// CALP inserts one RTCCLK pulse every 2048 pulses: 1e9 / 2048 = 488281 ppb.
	static constexpr int64 CalpPpb = 488281;
	// The 32 s calibration window counts 2^20 RTCCLK pulses; each CALM pulse removed slows the clock by 1e9 / 2^20 ppb.
	static constexpr int64 CalibrationWindowPulses = 1048576;
	static constexpr int64 CalibrationMinusMax = 511;
	static constexpr int64 PpbScale = 1000000000;
	// The wakeup counter is 16 bits; with ck_spre and WUCKSEL = 11x the counter gets an extra 2^16.
	static constexpr uint32 WakeupCounterMax = 0x10000;
	static constexpr uint32 WakeupCounterExtended = 0x20000;
	// DBP state seen by the last UnlockRegisters(), so LockRegisters() can put it back.
	bool backupAccessWasEnabled = false;


public:
	RTCAdapterF4() = default;
	RTCAdapterF4(RTC_TypeDef *rtc, uint32 clockSourceHz): RTCAdapter(rtc, clockSourceHz) {
	}


	virtual bool IsTimeSet() const override {
		return LL_RTC_IsActiveFlag_INITS(rtcHandle) != 0;
	}


	virtual ResultStatus SetDateTime(const DateTime& dateTime) override {
		if (!IsValidDateTime(dateTime)) {
			return ResultStatus::invalidParameter;
		}

		if (!IsClockReady()) {
			return ResultStatus::notReady;
		}

		UnlockRegisters();
		if (!EnterInitMode()) {
			LockRegisters();
			return ResultStatus::timeout;
		}

		// The calendar is only writable in init mode; the year field is the two-digit offset from 2000.
		Weekday weekday = CalculateWeekday(dateTime.year, dateTime.month, dateTime.day);
		LL_RTC_TIME_Config(
			rtcHandle,
			LL_RTC_HOURFORMAT_24HOUR,
			ToBcd(dateTime.hours),
			ToBcd(dateTime.minutes),
			ToBcd(dateTime.seconds)
		);
		LL_RTC_DATE_Config(
			rtcHandle,
			static_cast<uint32>(weekday),
			ToBcd(dateTime.day),
			ToBcd(dateTime.month),
			ToBcd(dateTime.year - 2000)
		);

		bool synced = LeaveInitMode();
		LockRegisters();

		return synced ? ResultStatus::ok : ResultStatus::timeout;
	}


	virtual Result<DateTime> GetDateTime() const override {
		if (!IsTimeSet()) {
			return ResultStatus::notReady;
		}

		// The shadow registers are only valid once RSF is set. After a wake from STOP or standby RSF is cleared until the
		// shadow registers resynchronise, so the read waits for it instead of returning a stale time or date.
		if (!WaitForFlag(LL_RTC_IsActiveFlag_RS, 1)) {
			return ResultStatus::timeout;
		}

		// TR locks the shadow registers and DR unlocks them, so the time is read first and the date last. SSR is read
		// between them, while the shadow set is still locked, so the fraction belongs to the same instant.
		uint32 hoursBcd = LL_RTC_TIME_GetHour(rtcHandle);
		uint32 minutesBcd = LL_RTC_TIME_GetMinute(rtcHandle);
		uint32 secondsBcd = LL_RTC_TIME_GetSecond(rtcHandle);
		uint32 subSecond = LL_RTC_TIME_GetSubSecond(rtcHandle);
		uint32 yearBcd = LL_RTC_DATE_GetYear(rtcHandle);
		uint32 monthBcd = LL_RTC_DATE_GetMonth(rtcHandle);
		uint32 dayBcd = LL_RTC_DATE_GetDay(rtcHandle);
		uint32 weekDay = LL_RTC_DATE_GetWeekDay(rtcHandle);

		// SSR counts down from the synchronous prescaler to zero; the elapsed part of the second is the fraction.
		uint64 syncPrediv = LL_RTC_GetSynchPrescaler(rtcHandle);
		uint64 elapsed = syncPrediv - subSecond;

		DateTime dateTime;
		dateTime.year = static_cast<uint16>(2000 + FromBcd(yearBcd));
		dateTime.month = FromBcd(monthBcd);
		dateTime.day = FromBcd(dayBcd);
		dateTime.weekday = static_cast<Weekday>(weekDay);
		dateTime.hours = FromBcd(hoursBcd);
		dateTime.minutes = FromBcd(minutesBcd);
		dateTime.seconds = FromBcd(secondsBcd);
		dateTime.microseconds = static_cast<uint32>((elapsed * 1000000) / (syncPrediv + 1));

		return dateTime;
	}


	virtual uint8 GetAlarmCount() const override {
		return AlarmCount;
	}


	virtual ResultStatus SetAlarm(uint8 index, const Alarm& alarm) override {
		if (index >= AlarmCount) {
			return ResultStatus::outOfRange;
		}

		if (!IsValidAlarm(alarm)) {
			return ResultStatus::invalidParameter;
		}

		if (!IsClockReady()) {
			return ResultStatus::notReady;
		}

		UnlockRegisters();
		bool written = (index == 0) ? WriteAlarmA(alarm) : WriteAlarmB(alarm);
		LockRegisters();

		return written ? ResultStatus::ok : ResultStatus::timeout;
	}


	virtual ResultStatus DisableAlarm(uint8 index) override {
		if (index >= AlarmCount) {
			return ResultStatus::outOfRange;
		}

		if (!IsClockReady()) {
			return ResultStatus::notReady;
		}

		UnlockRegisters();
		bool disabled = (index == 0) ? DisableAlarmA() : DisableAlarmB();
		LockRegisters();

		return disabled ? ResultStatus::ok : ResultStatus::timeout;
	}


	virtual ResultStatus SetWakeupTimer(uint32 periodMs) override {
		uint32 clockSelect = 0;
		uint32 reload = 0;
		if (!CalculateWakeup(periodMs, clockSelect, reload)) {
			return ResultStatus::invalidParameter;
		}

		if (!IsClockReady()) {
			return ResultStatus::notReady;
		}

		UnlockRegisters();

		// WUT and WUCKSEL can only change while the wakeup timer is stopped, and WUTWF says the write is accepted.
		LL_RTC_WAKEUP_Disable(rtcHandle);
		if (!WaitForFlag(LL_RTC_IsActiveFlag_WUTW, 1)) {
			LockRegisters();
			return ResultStatus::timeout;
		}

		LL_RTC_WAKEUP_SetClock(rtcHandle, clockSelect);
		LL_RTC_WAKEUP_SetAutoReload(rtcHandle, reload);
		LL_RTC_ClearFlag_WUT(rtcHandle);
		LL_RTC_EnableIT_WUT(rtcHandle);
		LL_RTC_WAKEUP_Enable(rtcHandle);

		LockRegisters();

		return ResultStatus::ok;
	}


	virtual ResultStatus DisableWakeupTimer() override {
		if (!IsClockReady()) {
			return ResultStatus::notReady;
		}

		UnlockRegisters();

		LL_RTC_WAKEUP_Disable(rtcHandle);
		bool stopped = WaitForFlag(LL_RTC_IsActiveFlag_WUTW, 1);
		LL_RTC_DisableIT_WUT(rtcHandle);
		LL_RTC_ClearFlag_WUT(rtcHandle);

		LockRegisters();

		return stopped ? ResultStatus::ok : ResultStatus::timeout;
	}


	virtual uint8 GetBackupRegisterCount() const override {
		return BackupRegisterCount;
	}


	virtual ResultStatus WriteBackupRegister(uint8 index, uint32 value) override {
		if (index >= BackupRegisterCount) {
			return ResultStatus::outOfRange;
		}

		// Without the PWR clock the DBP write is dropped silently, so the backup write would be lost while still reporting ok.
		if (LL_APB1_GRP1_IsEnabledClock(LL_APB1_GRP1_PERIPH_PWR) == 0) {
			return ResultStatus::notReady;
		}

		// The backup registers sit behind DBP only; the RTC write protection in WPR does not cover them. DBP is put back
		// to its previous state afterwards, as the register lock does.
		bool wasEnabled = LL_PWR_IsEnabledBkUpAccess() != 0;
		LL_PWR_EnableBkUpAccess();
		LL_RTC_BAK_SetRegister(rtcHandle, index, value);
		if (!wasEnabled) {
			LL_PWR_DisableBkUpAccess();
		}

		return ResultStatus::ok;
	}


	virtual Result<uint32> ReadBackupRegister(uint8 index) const override {
		if (index >= BackupRegisterCount) {
			return ResultStatus::outOfRange;
		}

		return LL_RTC_BAK_GetRegister(rtcHandle, index);
	}


	// Runs in interrupt context. The callbacks (onAlarm, onWakeup) must only set a flag or post to a queue. They must not
	// call SetDateTime, SetAlarm, SetWakeupTimer or the Disable* methods: those spin on RTC flags, and a nested
	// LockRegisters() would drop the interrupted thread's remaining RTC writes.
	// A flag is cleared whether or not its interrupt is enabled, so a disarmed source does not keep the vector firing. The
	// callback runs only when the matching enable bit (ALRAIE, ALRBIE, WUTIE) is set.
	virtual void IrqHandler() override {
		// The EXTI pending bits are cleared before the flags are read and regardless of them. An application can disarm an
		// alarm between the EXTI edge and this handler, which clears the flag; clearing the pending bit only for an active
		// flag would then leave it latched and the vector would keep re-entering. An edge that arrives after the clear
		// latches the pending bit again, and the next entry serves it.
		LL_EXTI_ClearFlag_0_31(LL_EXTI_LINE_17);
		LL_EXTI_ClearFlag_0_31(LL_EXTI_LINE_22);

		if (LL_RTC_IsActiveFlag_ALRA(rtcHandle) != 0) {
			LL_RTC_ClearFlag_ALRA(rtcHandle);

			if (LL_RTC_IsEnabledIT_ALRA(rtcHandle) != 0) {
				CallAlarm(0);
			}
		}

		if (LL_RTC_IsActiveFlag_ALRB(rtcHandle) != 0) {
			LL_RTC_ClearFlag_ALRB(rtcHandle);

			if (LL_RTC_IsEnabledIT_ALRB(rtcHandle) != 0) {
				CallAlarm(1);
			}
		}

		if (LL_RTC_IsActiveFlag_WUT(rtcHandle) != 0) {
			LL_RTC_ClearFlag_WUT(rtcHandle);

			if (LL_RTC_IsEnabledIT_WUT(rtcHandle) != 0) {
				CallWakeup();
			}
		}
	}


protected:
	virtual ResultStatus Initialization() override {
		ResultStatus status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		if (!IsClockReady()) {
			return ResultStatus::notReady;
		}

		// Everything is computed before the registers are unlocked, so a bad parameter leaves the RTC untouched.
		uint32 asyncPrediv = 0;
		uint32 syncPrediv = 0;
		if (!CalculatePrescalers(asyncPrediv, syncPrediv)) {
			return ResultStatus::invalidParameter;
		}

		uint32 calibrationPulse = 0;
		uint32 calibrationMinus = 0;
		if (!CalculateCalibration(parameters.calibrationPpb, calibrationPulse, calibrationMinus)) {
			return ResultStatus::invalidParameter;
		}

		UnlockRegisters();

		// The prescalers are only writable in init mode. Init mode stops the calendar but keeps its value, so the time
		// set earlier survives the change.
		if (!EnterInitMode()) {
			LockRegisters();
			return ResultStatus::timeout;
		}

		LL_RTC_SetAsynchPrescaler(rtcHandle, asyncPrediv);
		LL_RTC_SetSynchPrescaler(rtcHandle, syncPrediv);
		bool synced = LeaveInitMode();

		// A new CALR is only accepted once the previous recalibration has been applied (RECALPF clear).
		bool calibrated = synced && WaitForFlag(LL_RTC_IsActiveFlag_RECALP, 0);
		if (calibrated) {
			uint32 insertPulse = (calibrationPulse != 0) ? LL_RTC_CALIB_INSERTPULSE_SET : LL_RTC_CALIB_INSERTPULSE_NONE;

			// CALR is written as one word. Writing it sets RECALPF, which blocks further CALR writes, so separate
			// period, pulse and minus writes would be partly dropped. The 32 s period is the reset value (CALW8 = CALW16 = 0).
			WRITE_REG(rtcHandle->CALR, LL_RTC_CALIB_PERIOD_32SEC | insertPulse | calibrationMinus);
		}

		LockRegisters();

		if (!synced || !calibrated) {
			return ResultStatus::timeout;
		}

		return AfterInitialization();
	}


private:
	void UnlockRegisters() {
		backupAccessWasEnabled = LL_PWR_IsEnabledBkUpAccess() != 0;
		LL_PWR_EnableBkUpAccess();
		LL_RTC_DisableWriteProtection(rtcHandle);
	}


	void LockRegisters() {
		LL_RTC_EnableWriteProtection(rtcHandle);

		if (!backupAccessWasEnabled) {
			LL_PWR_DisableBkUpAccess();
		}
	}


	bool IsClockReady() const {
		if (LL_RCC_GetRTCClockSource() == LL_RCC_RTC_CLKSOURCE_NONE) {
			return false;
		}

		if (LL_RCC_IsEnabledRTC() == 0) {
			return false;
		}

		return LL_APB1_GRP1_IsEnabledClock(LL_APB1_GRP1_PERIPH_PWR) != 0;
	}


	bool WaitForFlag(FlagReader readFlag, uint32 expected) const {
		for (uint32 i = 0; i < FlagWaitLoops; ++i) {
			if (readFlag(rtcHandle) == expected) {
				return true;
			}
		}

		return false;
	}


	bool EnterInitMode() {
		LL_RTC_EnableInitMode(rtcHandle);
		if (WaitForFlag(LL_RTC_IsActiveFlag_INIT, 1)) {
			return true;
		}

		// INIT left set freezes the calendar and stops alarms and wakeup until something clears it, so it is cleared here
		// before the timeout is reported.
		LL_RTC_DisableInitMode(rtcHandle);
		return false;
	}


	bool LeaveInitMode() {
		LL_RTC_DisableInitMode(rtcHandle);

		// RSF is set by hardware once the shadow registers reflect the new state; clear it first so the wait is real.
		LL_RTC_ClearFlag_RS(rtcHandle);
		return WaitForFlag(LL_RTC_IsActiveFlag_RS, 1);
	}


	bool WriteAlarmA(const Alarm& alarm) {
		// ALRAE and ALRMAR are only writable while ALRAWF is set, so the alarm is stopped first.
		LL_RTC_ALMA_Disable(rtcHandle);
		if (!WaitForFlag(LL_RTC_IsActiveFlag_ALRAW, 1)) {
			return false;
		}

		LL_RTC_ALMA_SetMask(rtcHandle, BuildAlarmMask(alarm.match));
		LL_RTC_ALMA_ConfigTime(
			rtcHandle,
			LL_RTC_ALMA_TIME_FORMAT_AM,
			ToBcd(alarm.time.hours),
			ToBcd(alarm.time.minutes),
			ToBcd(alarm.time.seconds)
		);

		if (alarm.match.byWeekday) {
			LL_RTC_ALMA_EnableWeekday(rtcHandle);
			LL_RTC_ALMA_SetWeekDay(rtcHandle, static_cast<uint32>(alarm.time.weekday));
		} else {
			LL_RTC_ALMA_DisableWeekday(rtcHandle);
			LL_RTC_ALMA_SetDay(rtcHandle, ToBcd(alarm.time.day));
		}

		LL_RTC_ClearFlag_ALRA(rtcHandle);
		LL_RTC_EnableIT_ALRA(rtcHandle);
		LL_RTC_ALMA_Enable(rtcHandle);

		return true;
	}


	bool WriteAlarmB(const Alarm& alarm) {
		LL_RTC_ALMB_Disable(rtcHandle);
		if (!WaitForFlag(LL_RTC_IsActiveFlag_ALRBW, 1)) {
			return false;
		}

		LL_RTC_ALMB_SetMask(rtcHandle, BuildAlarmMask(alarm.match));
		LL_RTC_ALMB_ConfigTime(
			rtcHandle,
			LL_RTC_ALMA_TIME_FORMAT_AM,
			ToBcd(alarm.time.hours),
			ToBcd(alarm.time.minutes),
			ToBcd(alarm.time.seconds)
		);

		if (alarm.match.byWeekday) {
			LL_RTC_ALMB_EnableWeekday(rtcHandle);
			LL_RTC_ALMB_SetWeekDay(rtcHandle, static_cast<uint32>(alarm.time.weekday));
		} else {
			LL_RTC_ALMB_DisableWeekday(rtcHandle);
			LL_RTC_ALMB_SetDay(rtcHandle, ToBcd(alarm.time.day));
		}

		LL_RTC_ClearFlag_ALRB(rtcHandle);
		LL_RTC_EnableIT_ALRB(rtcHandle);
		LL_RTC_ALMB_Enable(rtcHandle);

		return true;
	}


	bool DisableAlarmA() {
		LL_RTC_ALMA_Disable(rtcHandle);
		bool stopped = WaitForFlag(LL_RTC_IsActiveFlag_ALRAW, 1);
		LL_RTC_DisableIT_ALRA(rtcHandle);
		LL_RTC_ClearFlag_ALRA(rtcHandle);

		return stopped;
	}


	bool DisableAlarmB() {
		LL_RTC_ALMB_Disable(rtcHandle);
		bool stopped = WaitForFlag(LL_RTC_IsActiveFlag_ALRBW, 1);
		LL_RTC_DisableIT_ALRB(rtcHandle);
		LL_RTC_ClearFlag_ALRB(rtcHandle);

		return stopped;
	}


	// Alarm A and B share the bit layout of ALRMAR and ALRMBR, so one mask builder serves both.
	static uint32 BuildAlarmMask(const AlarmMatch& match) {
		uint32 mask = LL_RTC_ALMA_MASK_NONE;

		if (!match.seconds) {
			mask |= LL_RTC_ALMA_MASK_SECONDS;
		}

		if (!match.minutes) {
			mask |= LL_RTC_ALMA_MASK_MINUTES;
		}

		if (!match.hours) {
			mask |= LL_RTC_ALMA_MASK_HOURS;
		}

		if (!match.day) {
			mask |= LL_RTC_ALMA_MASK_DATEWEEKDAY;
		}

		return mask;
	}


	// Picks the finest wakeup clock that counts the period within 16 bits, then ck_spre for longer periods. The rounding
	// is to the nearest tick, so the period error is at most half a tick of the chosen clock.
	bool CalculateWakeup(uint32 periodMs, uint32& clockSelect, uint32& reload) const {
		if (periodMs == 0) {
			return false;
		}

		const uint32 dividers[4] = {
			2,
			4,
			8,
			16
		};
		const uint32 selects[4] = {
			LL_RTC_WAKEUPCLOCK_DIV_2,
			LL_RTC_WAKEUPCLOCK_DIV_4,
			LL_RTC_WAKEUPCLOCK_DIV_8,
			LL_RTC_WAKEUPCLOCK_DIV_16
		};

		for (uint8 i = 0; i < 4; ++i) {
			uint64 clockHz = inputClockHz / dividers[i];
			uint64 ticks = (static_cast<uint64>(periodMs) * clockHz + 500) / 1000;
			if (ticks >= 1 && ticks <= WakeupCounterMax) {
				clockSelect = selects[i];
				reload = static_cast<uint32>(ticks - 1);
				return true;
			}
		}

		uint64 seconds = (static_cast<uint64>(periodMs) + 500) / 1000;
		if (seconds < 1) {
			seconds = 1;
		}

		if (seconds <= WakeupCounterMax) {
			clockSelect = LL_RTC_WAKEUPCLOCK_CKSPRE;
			reload = static_cast<uint32>(seconds - 1);
			return true;
		}

		// WUCKSEL = 11x adds 2^16 to the counter, so the period is WUT + 1 + 2^16 seconds.
		if (seconds <= WakeupCounterExtended) {
			clockSelect = LL_RTC_WAKEUPCLOCK_CKSPRE_WUT;
			reload = static_cast<uint32>(seconds - WakeupCounterMax - 1);
			return true;
		}

		return false;
	}


	// Prefers the largest asynchronous divider so the synchronous part stays within its 15 bits; 32.768 kHz gives
	// 128 * 256, which is exactly 1 Hz.
	bool CalculatePrescalers(uint32& asyncPrediv, uint32& syncPrediv) const {
		if (inputClockHz == 0) {
			return false;
		}

		for (uint32 divider = AsyncPredivMax; divider >= 1; --divider) {
			if (inputClockHz % divider != 0) {
				continue;
			}

			uint32 quotient = inputClockHz / divider;
			if (quotient - 1 <= SyncPredivMax) {
				asyncPrediv = divider - 1;
				syncPrediv = quotient - 1;
				return true;
			}
		}

		return false;
	}


	// Chooses CALP (one extra pulse per 2048, about +488 ppm) and the CALM pulses removed per window, whichever lands
	// closer to the target. The positive limit comes from CALP; the negative limit from removing every CALM pulse.
	static bool CalculateCalibration(int32 ppb, uint32& pulse, uint32& minus) {
		int64 target = ppb;
		if (target > CalpPpb || target < -(CalibrationMinusMax * PpbScale / CalibrationWindowPulses)) {
			return false;
		}

		bool found = false;
		int64 bestError = 0;

		for (uint32 candidate = 0; candidate <= 1; ++candidate) {
			int64 base = (candidate != 0) ? CalpPpb : 0;
			int64 needed = 0;
			if (base > target) {
				needed = ((base - target) * CalibrationWindowPulses + PpbScale / 2) / PpbScale;
			}

			if (needed > CalibrationMinusMax) {
				continue;
			}

			int64 achieved = base - needed * PpbScale / CalibrationWindowPulses;
			int64 error = (achieved > target) ? (achieved - target) : (target - achieved);

			if (!found || error < bestError) {
				found = true;
				bestError = error;
				pulse = candidate;
				minus = static_cast<uint32>(needed);
			}
		}

		return found;
	}


	static bool IsValidDateTime(const DateTime& dateTime) {
		if (dateTime.year < 2000 || dateTime.year > 2099) {
			return false;
		}

		if (dateTime.month < 1 || dateTime.month > 12) {
			return false;
		}

		if (dateTime.day < 1 || dateTime.day > DaysInMonth(dateTime.year, dateTime.month)) {
			return false;
		}

		return dateTime.hours <= 23 && dateTime.minutes <= 59 && dateTime.seconds <= 59;
	}


	static bool IsValidAlarm(const Alarm& alarm) {
		if (alarm.time.hours > 23 || alarm.time.minutes > 59 || alarm.time.seconds > 59) {
			return false;
		}

		if (alarm.match.byWeekday) {
			return alarm.time.weekday >= Weekday::Monday && alarm.time.weekday <= Weekday::Sunday;
		}

		return alarm.time.day >= 1 && alarm.time.day <= 31;
	}


	// The RTC registers hold BCD; the conversions keep the public API in plain binary.
	static uint32 ToBcd(uint32 value) {
		return ((value / 10) << 4) | (value % 10);
	}


	static uint8 FromBcd(uint32 value) {
		return static_cast<uint8>(((value >> 4) & 0x0F) * 10 + (value & 0x0F));
	}
};
