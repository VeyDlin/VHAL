#pragma once
#include <Adapter/RTCAdapter.h>


// STM32 real-time clock for the G0 and G4 families (RTC v3 register block with the TAMP backup registers).
// The family file supplies the EXTI lines and the number of backup registers; everything else is shared here.
//
// What the BSP must do before the adapter is used. The adapter neither configures the clock tree nor the NVIC.
//   1. Enable the PWR and RTC APB clocks (G4: RCC APB1ENR1 PWREN and RTCAPBEN; G0: RCC APBENR1 PWREN and RTCAPBEN).
//      Without them every call returns notReady.
//   2. Start LSE or LSI, select it as the RTC clock (RCC BDCR: RTCSEL) and enable the RTC clock (RCC BDCR: RTCEN).
//      The constructor takes the frequency of that source. The port derives a 1 Hz calendar tick from it, so the
//      frequency must be expressible with the two prescalers (see CalculatePrescalers()).
//   3. Enable the RTC vectors listed in the family file in the NVIC and call IrqHandler() from each of them.
//   4. Configure the EXTI lines listed in the family file as interrupt lines with a rising-edge trigger. IrqHandler()
//      clears their pending bits, because a pending bit left set keeps the vector firing.
//   5. Call SetParameters() once at boot, before the calendar is set. It pauses the calendar for the length of its
//      init-mode session and moves the phase of the 1 Hz tick, so it is not meant to run while the calendar is in use.
//
// The adapter opens the backup domain (PWR DBP) and lifts the RTC write protection around each write, then restores
// the state it found. The application does not need to unlock anything.
//
// The adapter never selects the clock source and never starts the LSE or LSI; that belongs to the BSP like every
// other clock.


// Opens the backup domain and lifts the RTC write protection for the lifetime of the object. On destruction the
// write protection is restored and the backup domain returns to the state it had, so an application that locked the
// backup domain keeps it locked.
class RTCv3WriteAccess {
private:
	RTC_TypeDef *rtcHandle = nullptr;
	bool backupWasEnabled = false;


public:
	explicit RTCv3WriteAccess(RTC_TypeDef *rtc) : rtcHandle(rtc), backupWasEnabled(LL_PWR_IsEnabledBkUpAccess() != 0) {
		if (!backupWasEnabled) {
			LL_PWR_EnableBkUpAccess();
		}

		LL_RTC_DisableWriteProtection(rtcHandle);
	}


	~RTCv3WriteAccess() {
		LL_RTC_EnableWriteProtection(rtcHandle);

		if (!backupWasEnabled) {
			LL_PWR_DisableBkUpAccess();
		}
	}


	RTCv3WriteAccess(const RTCv3WriteAccess&) = delete;
	RTCv3WriteAccess& operator=(const RTCv3WriteAccess&) = delete;
};



// alarmExtiLine and wakeupExtiLine are the LL_EXTI_LINE_xx masks of the lines the alarms and the wakeup timer use.
// backupRegisterCount is the number of TAMP backup registers the chip declares in its CMSIS header.
template<uint32 AlarmExtiLine, uint32 WakeupExtiLine, uint8 BackupRegisterCount>
class RTCv3STM32: public RTCAdapter<RTC_TypeDef> {
public:
	// Alarm A and alarm B. Both report through the same EXTI line.
	static inline constexpr uint8 AlarmCount = 2;
	// Smooth calibration corrects in 2^20 RTCCLK cycles (32 s at 32768 Hz). CALM removes one cycle per window.
	static inline constexpr uint32 CalibrationWindowCycles = 1048576;
	// CALP inserts one RTCCLK pulse every 2^11 cycles, which is +488281 ppb over the window.
	static inline constexpr uint32 CalibrationPulsePpb = 488281;
	// Smallest correction with CALP on: CalibrationPulsePpb minus the largest CALM count (511 * 953.67 ppb).
	static inline constexpr uint32 CalibrationMinPulsePpb = 954;
	// CALM is a 9-bit field.
	static inline constexpr uint32 CalibrationMaxCount = 511;
	// G4 names the RTC APB clock RTCAPB; G0 names the same enable bit RTC.
#if defined(LL_APB1_GRP1_PERIPH_RTCAPB)
	static inline constexpr uint32 RtcApbClock = LL_APB1_GRP1_PERIPH_RTCAPB;
#else
	static inline constexpr uint32 RtcApbClock = LL_APB1_GRP1_PERIPH_RTC;
#endif
	// Upper bound for every poll on an RTC flag: init mode, alarm and wakeup writes, and calibration. The flags settle
	// within a few RTC cycles, so the bound is reached only when the RTC clock is not running. The poll then gives up
	// with timeout instead of hanging the caller.
	static inline constexpr uint32 WaitLoopCount = 100000;


public:
	RTCv3STM32() = default;


	RTCv3STM32(RTC_TypeDef *rtc, uint32 clockSourceHz) : RTCAdapter<RTC_TypeDef>(rtc, clockSourceHz) {
	}


	// INITS is set by the hardware once the calendar has been written, and stays set across resets while the backup
	// domain keeps power.
	virtual bool IsTimeSet() const override {
		return IsClockEnabled() && LL_RTC_IsActiveFlag_INITS(rtcHandle) != 0;
	}


	virtual ResultStatus SetDateTime(const DateTime& dateTime) override {
		ResultStatus status = ValidateDateTime(dateTime);
		if (status != ResultStatus::ok) {
			return status;
		}

		if (!IsClockEnabled()) {
			return ResultStatus::notReady;
		}

		uint32 bcdSeconds = ToBcd(dateTime.seconds);
		uint32 bcdMinutes = ToBcd(dateTime.minutes);
		uint32 bcdHours = ToBcd(dateTime.hours);
		uint32 bcdDay = ToBcd(dateTime.day);
		uint32 bcdMonth = ToBcd(dateTime.month);
		uint32 bcdYear = ToBcd(static_cast<uint8>(dateTime.year - 2000));
		uint32 weekday = static_cast<uint32>(CalculateWeekday(dateTime.year, dateTime.month, dateTime.day));

		RTCv3WriteAccess access(rtcHandle);
		if (!EnterInitMode()) {
			return ResultStatus::timeout;
		}

		LL_RTC_TIME_Config(rtcHandle, LL_RTC_TIME_FORMAT_AM_OR_24, bcdHours, bcdMinutes, bcdSeconds);
		LL_RTC_DATE_Config(rtcHandle, weekday, bcdDay, bcdMonth, bcdYear);
		LL_RTC_ExitInitMode(rtcHandle);

		return ResultStatus::ok;
	}


	virtual Result<DateTime> GetDateTime() const override {
		if (!IsTimeSet()) {
			return ResultStatus::notReady;
		}

		// Reading TIME locks the shadow registers and reading DATE releases them. The subseconds are read between the
		// two so that all fields belong to the same second.
		uint32 timeRegister = LL_RTC_TIME_Get(rtcHandle);
		uint32 subSecond = LL_RTC_TIME_GetSubSecond(rtcHandle);
		uint32 dateRegister = LL_RTC_DATE_Get(rtcHandle);
		uint32 syncPrescaler = LL_RTC_GetSynchPrescaler(rtcHandle);

		// SSR counts down from the synchronous prescaler to 0, so the elapsed part of the second is the difference.
		if (subSecond > syncPrescaler) {
			subSecond = syncPrescaler;
		}

		uint32 elapsed = syncPrescaler - subSecond;

		DateTime dateTime;
		dateTime.year = static_cast<uint16>(2000 + FromBcd(__LL_RTC_GET_YEAR(dateRegister)));
		dateTime.month = FromBcd(__LL_RTC_GET_MONTH(dateRegister));
		dateTime.day = FromBcd(__LL_RTC_GET_DAY(dateRegister));
		dateTime.weekday = static_cast<Weekday>(__LL_RTC_GET_WEEKDAY(dateRegister));
		dateTime.hours = FromBcd(__LL_RTC_GET_HOUR(timeRegister));
		dateTime.minutes = FromBcd(__LL_RTC_GET_MINUTE(timeRegister));
		dateTime.seconds = FromBcd(__LL_RTC_GET_SECOND(timeRegister));
		dateTime.microseconds = static_cast<uint32>(static_cast<uint64>(elapsed) * 1000000ULL / (syncPrescaler + 1));

		return dateTime;
	}


	virtual uint8 GetAlarmCount() const override {
		return AlarmCount;
	}


	// The alarm registers can only be written while the alarm is disabled and the hardware has acknowledged that.
	virtual ResultStatus SetAlarm(uint8 index, const Alarm& alarm) override {
		if (index >= AlarmCount) {
			return ResultStatus::outOfRange;
		}

		ResultStatus status = ValidateAlarm(alarm);
		if (status != ResultStatus::ok) {
			return status;
		}

		if (!IsClockEnabled()) {
			return ResultStatus::notReady;
		}

		RTCv3WriteAccess access(rtcHandle);
		DisableAlarmHardware(index);
		if (!WaitForAlarmWritable(index)) {
			return ResultStatus::timeout;
		}

		uint32 value = ToBcd(alarm.time.seconds) << RTC_ALRMAR_SU_Pos;
		value |= ToBcd(alarm.time.minutes) << RTC_ALRMAR_MNU_Pos;
		value |= ToBcd(alarm.time.hours) << RTC_ALRMAR_HU_Pos;

		// A set MSK bit makes that field a don't-care, so the mask is the inverse of the match flags.
		if (!alarm.match.seconds) {
			value |= RTC_ALRMAR_MSK1;
		}

		if (!alarm.match.minutes) {
			value |= RTC_ALRMAR_MSK2;
		}

		if (!alarm.match.hours) {
			value |= RTC_ALRMAR_MSK3;
		}

		if (!alarm.match.day) {
			value |= RTC_ALRMAR_MSK4;
		}

		// With WDSEL set the DU field holds the weekday (1 to 7) instead of the day of the month.
		if (alarm.match.byWeekday) {
			value |= RTC_ALRMAR_WDSEL | (static_cast<uint32>(alarm.time.weekday) << RTC_ALRMAR_DU_Pos);
		} else {
			value |= ToBcd(alarm.time.day) << RTC_ALRMAR_DU_Pos;
		}

		// The subseconds are not part of the match, so their mask is set to all don't-care.
		volatile uint32 *alarmRegister = (index == 0) ? &rtcHandle->ALRMAR : &rtcHandle->ALRMBR;
		volatile uint32 *subSecondRegister = (index == 0) ? &rtcHandle->ALRMASSR : &rtcHandle->ALRMBSSR;
		*alarmRegister = value;
		*subSecondRegister = RTC_ALRMASSR_MASKSS;

		EnableAlarmHardware(index);
		return ResultStatus::ok;
	}


	virtual ResultStatus DisableAlarm(uint8 index) override {
		if (index >= AlarmCount) {
			return ResultStatus::outOfRange;
		}

		if (!IsClockEnabled()) {
			return ResultStatus::notReady;
		}

		RTCv3WriteAccess access(rtcHandle);
		DisableAlarmHardware(index);
		return ResultStatus::ok;
	}


	// The wakeup timer runs from RTCCLK/16 while the period fits its 16-bit counter, which keeps the resolution at
	// about 0.5 ms. Longer periods switch to ck_spre (1 Hz), which counts whole seconds up to 2^17 s, so above 32 s
	// only periods that are a whole number of seconds are accepted.
	virtual ResultStatus SetWakeupTimer(uint32 periodMs) override {
		if (periodMs == 0) {
			return ResultStatus::invalidParameter;
		}

		if (inputClockHz == 0) {
			return ResultStatus::invalidParameter;
		}

		if (!IsClockEnabled()) {
			return ResultStatus::notReady;
		}

		uint64 ticks = (static_cast<uint64>(periodMs) * inputClockHz + 8000ULL) / 16000ULL;
		if (ticks == 0) {
			ticks = 1;
		}

		uint32 clockSelect = LL_RTC_WAKEUPCLOCK_DIV_16;
		uint32 reload = 0;
		if (ticks <= 65536ULL) {
			reload = static_cast<uint32>(ticks - 1);
		} else {
			// ck_spre ticks once per second, so a period that is not a whole number of seconds cannot be made exactly.
			if (periodMs % 1000U != 0) {
				return ResultStatus::invalidParameter;
			}

			uint64 seconds = static_cast<uint64>(periodMs) / 1000ULL;
			if (seconds <= 65536ULL) {
				clockSelect = LL_RTC_WAKEUPCLOCK_CKSPRE;
				reload = static_cast<uint32>(seconds - 1);
			} else if (seconds <= 131072ULL) {
				// This clock selection adds 2^16 to the counter, so the reload covers 65537 to 131072 seconds.
				clockSelect = LL_RTC_WAKEUPCLOCK_CKSPRE_WUT;
				reload = static_cast<uint32>(seconds - 65537ULL);
			} else {
				return ResultStatus::invalidParameter;
			}
		}

		RTCv3WriteAccess access(rtcHandle);
		LL_RTC_DisableIT_WUT(rtcHandle);
		LL_RTC_WAKEUP_Disable(rtcHandle);
		if (!WaitForWakeupWritable()) {
			return ResultStatus::timeout;
		}

		LL_RTC_WAKEUP_SetClock(rtcHandle, clockSelect);
		LL_RTC_WAKEUP_SetAutoReload(rtcHandle, reload);
		LL_RTC_ClearFlag_WUT(rtcHandle);
		LL_RTC_EnableIT_WUT(rtcHandle);
		LL_RTC_WAKEUP_Enable(rtcHandle);

		return ResultStatus::ok;
	}


	virtual ResultStatus DisableWakeupTimer() override {
		if (!IsClockEnabled()) {
			return ResultStatus::notReady;
		}

		RTCv3WriteAccess access(rtcHandle);
		LL_RTC_DisableIT_WUT(rtcHandle);
		LL_RTC_WAKEUP_Disable(rtcHandle);
		if (!WaitForWakeupWritable()) {
			return ResultStatus::timeout;
		}

		LL_RTC_ClearFlag_WUT(rtcHandle);
		return ResultStatus::ok;
	}


	// The backup registers live in TAMP. Every G0 and G4 CMSIS header in the repo defines TAMP; the gate only protects
	// a chip variant that might lack it, and such a chip reports zero registers.
	virtual uint8 GetBackupRegisterCount() const override {
#if defined(TAMP)
		return BackupRegisterCount;
#else
		return 0;
#endif
	}


	virtual ResultStatus WriteBackupRegister(uint8 index, uint32 value) override {
		if (index >= GetBackupRegisterCount()) {
			return ResultStatus::outOfRange;
		}

		if (!IsClockEnabled()) {
			return ResultStatus::notReady;
		}

#if defined(TAMP)
		RTCv3WriteAccess access(rtcHandle);
		volatile uint32 *registers = &TAMP->BKP0R;
		registers[index] = value;
		return ResultStatus::ok;
#else
		static_cast<void>(value);
		return ResultStatus::notSupported;
#endif
	}


	virtual Result<uint32> ReadBackupRegister(uint8 index) const override {
		if (index >= GetBackupRegisterCount()) {
			return ResultStatus::outOfRange;
		}

		if (!IsClockEnabled()) {
			return ResultStatus::notReady;
		}

#if defined(TAMP)
		const volatile uint32 *registers = &TAMP->BKP0R;
		uint32 value = registers[index];
		return value;
#else
		return ResultStatus::notSupported;
#endif
	}


	// Serves the alarm and wakeup flags. The masked flags only report sources that are enabled, so a vector shared by
	// several sources does not call back for the ones the application did not enable.
	virtual void IrqHandler() override {
		// The EXTI pending bits are cleared before the flags are read and regardless of them. An application can disarm
		// an alarm between the EXTI edge and this handler, which clears the masked flag; clearing the pending bit only
		// for an active flag would then leave it latched and the vector would keep re-entering. An edge that arrives
		// after the clear latches the pending bit again, and the next entry serves it.
		ClearExtiPending(AlarmExtiLine);
		ClearExtiPending(WakeupExtiLine);

		if (LL_RTC_IsActiveFlag_ALRAM(rtcHandle) != 0) {
			LL_RTC_ClearFlag_ALRA(rtcHandle);
			CallAlarm(0);
		}

		if (LL_RTC_IsActiveFlag_ALRBM(rtcHandle) != 0) {
			LL_RTC_ClearFlag_ALRB(rtcHandle);
			CallAlarm(1);
		}

		if (LL_RTC_IsActiveFlag_WUTM(rtcHandle) != 0) {
			LL_RTC_ClearFlag_WUT(rtcHandle);
			CallWakeup();
		}
	}


protected:
	// The prescalers can only be written in init mode, so they are written in one init-mode session, which freezes the
	// calendar for its length; the calendar restarts when the session ends. Writing the prescalers also moves the phase
	// of the 1 Hz tick, which is why the BSP calls SetParameters() once at boot.
	virtual ResultStatus Initialization() override {
		if (!IsClockEnabled()) {
			return ResultStatus::notReady;
		}

		uint32 asyncPrescaler = 0;
		uint32 syncPrescaler = 0;
		ResultStatus status = CalculatePrescalers(asyncPrescaler, syncPrescaler);
		if (status != ResultStatus::ok) {
			return status;
		}

		uint32 calibrationPulse = 0;
		uint32 calibrationCount = 0;
		status = CalculateCalibration(parameters.calibrationPpb, calibrationPulse, calibrationCount);
		if (status != ResultStatus::ok) {
			return status;
		}

		status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		{
			RTCv3WriteAccess access(rtcHandle);
			if (!EnterInitMode()) {
				return ResultStatus::timeout;
			}

			LL_RTC_SetAsynchPrescaler(rtcHandle, asyncPrescaler);
			LL_RTC_SetSynchPrescaler(rtcHandle, syncPrescaler);
			LL_RTC_SetHourFormat(rtcHandle, LL_RTC_HOURFORMAT_24HOUR);
			LL_RTC_ExitInitMode(rtcHandle);

			// A CALR write made just before is still taking effect until RECALP clears; a write in that window is lost,
			// so the calibration is written only once RECALP is clear. On timeout nothing of the calibration is written
			// and the caller can repeat SetParameters(), which rewrites the prescalers as well.
			if (!WaitForCalibrationReady()) {
				return ResultStatus::timeout;
			}

			LL_RTC_CAL_SetPeriod(rtcHandle, LL_RTC_CALIB_PERIOD_32SEC);
			LL_RTC_CAL_SetPulse(rtcHandle, calibrationPulse);
			LL_RTC_CAL_SetMinus(rtcHandle, calibrationCount);
		}

		return AfterInitialization();
	}


private:
	// Packs a value 0 to 99 into two BCD digits, which is the format the calendar and alarm registers take.
	static constexpr uint8 ToBcd(uint8 value) {
		return static_cast<uint8>(((value / 10) << 4) | (value % 10));
	}


	static constexpr uint8 FromBcd(uint32 value) {
		return static_cast<uint8>(((value >> 4) & 0x0FU) * 10U + (value & 0x0FU));
	}


	// The RTC clock must be enabled on the bus and in RCC, and the PWR clock is needed to open the backup domain. An
	// adapter built without a handle reports not ready here, so no public method dereferences a null handle.
	bool IsClockEnabled() const {
		return rtcHandle != nullptr
			&& LL_APB1_GRP1_IsEnabledClock(LL_APB1_GRP1_PERIPH_PWR) != 0
			&& LL_APB1_GRP1_IsEnabledClock(RtcApbClock) != 0
			&& LL_RCC_IsEnabledRTC() != 0;
	}


	// G0 splits the EXTI pending register into rising and falling halves; the RTC lines are rising-edge.
	static void ClearExtiPending(uint32 line) {
#if defined(EXTI_RPR1_RPIF0)
		LL_EXTI_ClearRisingFlag_0_31(line);
#else
		LL_EXTI_ClearFlag_0_31(line);
#endif
	}


	static bool IsTimeOfDayValid(const DateTime& dateTime) {
		return dateTime.hours < 24 && dateTime.minutes < 60 && dateTime.seconds < 60;
	}


	// The calendar runs from 2000 to 2099 because the year register holds two BCD digits.
	static ResultStatus ValidateDateTime(const DateTime& dateTime) {
		if (dateTime.year < 2000 || dateTime.year > 2099) {
			return ResultStatus::invalidParameter;
		}

		if (dateTime.month < 1 || dateTime.month > 12) {
			return ResultStatus::invalidParameter;
		}

		if (dateTime.day < 1 || dateTime.day > DaysInMonth(dateTime.year, dateTime.month)) {
			return ResultStatus::invalidParameter;
		}

		if (!IsTimeOfDayValid(dateTime)) {
			return ResultStatus::invalidParameter;
		}

		return ResultStatus::ok;
	}


	// An alarm carries no year or month, so its day is checked against the longest month only.
	static ResultStatus ValidateAlarm(const Alarm& alarm) {
		if (!IsTimeOfDayValid(alarm.time)) {
			return ResultStatus::invalidParameter;
		}

		if (alarm.match.byWeekday) {
			uint8 weekday = static_cast<uint8>(alarm.time.weekday);
			if (weekday < 1 || weekday > 7) {
				return ResultStatus::invalidParameter;
			}

			return ResultStatus::ok;
		}

		if (alarm.time.day < 1 || alarm.time.day > 31) {
			return ResultStatus::invalidParameter;
		}

		return ResultStatus::ok;
	}


	// Finds the largest asynchronous divider that divides the clock exactly. The largest divider gives the smallest
	// synchronous divider, and an inexact divider would make the 1 Hz calendar tick drift.
	ResultStatus CalculatePrescalers(uint32& asyncPrescaler, uint32& syncPrescaler) const {
		if (inputClockHz == 0) {
			return ResultStatus::invalidParameter;
		}

		uint32 divider = 128;
		while (divider > 1 && inputClockHz % divider != 0) {
			divider--;
		}

		uint32 sync = inputClockHz / divider - 1;
		if (sync > 0x7FFFU) {
			return ResultStatus::invalidParameter;
		}

		asyncPrescaler = divider - 1;
		syncPrescaler = sync;
		return ResultStatus::ok;
	}


	// Positive ppb speeds the clock up. Negative values use CALM alone; positive values use CALP (+488281 ppb) and CALM
	// to take back the excess. Results are rounded to the nearest CALM step.
	static ResultStatus CalculateCalibration(int32 ppb, uint32& pulse, uint32& count) {
		if (ppb <= 0) {
			uint64 magnitude = static_cast<uint64>(-static_cast<int64>(ppb));
			uint64 counts = (magnitude * CalibrationWindowCycles + 500000000ULL) / 1000000000ULL;
			if (counts > CalibrationMaxCount) {
				return ResultStatus::invalidParameter;
			}

			pulse = LL_RTC_CALIB_INSERTPULSE_NONE;
			count = static_cast<uint32>(counts);
			return ResultStatus::ok;
		}

		if (ppb > static_cast<int32>(CalibrationPulsePpb)) {
			return ResultStatus::invalidParameter;
		}

		// Below half of the smallest CALP-on setting, switching CALP off is the closer correction.
		if (ppb < static_cast<int32>(CalibrationMinPulsePpb / 2)) {
			pulse = LL_RTC_CALIB_INSERTPULSE_NONE;
			count = 0;
			return ResultStatus::ok;
		}

		uint64 excess = CalibrationPulsePpb - static_cast<uint32>(ppb);
		uint64 counts = (excess * CalibrationWindowCycles + 500000000ULL) / 1000000000ULL;
		if (counts > CalibrationMaxCount) {
			counts = CalibrationMaxCount;
		}

		pulse = LL_RTC_CALIB_INSERTPULSE_SET;
		count = static_cast<uint32>(counts);
		return ResultStatus::ok;
	}


	// The recalibration pending flag is set while a new CALR value is taking effect; writing CALR before it clears
	// would be lost. Returns false when the flag is still set after the bound.
	bool WaitForCalibrationReady() const {
		uint32 retries = WaitLoopCount;
		while (LL_RTC_IsActiveFlag_RECALP(rtcHandle) != 0 && retries > 0) {
			retries--;
		}

		return LL_RTC_IsActiveFlag_RECALP(rtcHandle) == 0;
	}


	// LL_RTC_EnterInitMode() counts its timeout with SysTick, so it hangs when SysTick is not running. This poll uses
	// WaitLoopCount instead. On timeout the INIT bit is cleared again; otherwise the calendar would stay frozen after
	// the RTC clock comes back.
	bool EnterInitMode() {
		LL_RTC_EnableInitMode(rtcHandle);

		uint32 retries = WaitLoopCount;
		while (LL_RTC_IsActiveFlag_INIT(rtcHandle) == 0 && retries > 0) {
			retries--;
		}

		if (LL_RTC_IsActiveFlag_INIT(rtcHandle) == 0) {
			LL_RTC_DisableInitMode(rtcHandle);
			return false;
		}

		return true;
	}


	void DisableAlarmHardware(uint8 index) {
		if (index == 0) {
			LL_RTC_ALMA_Disable(rtcHandle);
			LL_RTC_DisableIT_ALRA(rtcHandle);
			LL_RTC_ClearFlag_ALRA(rtcHandle);
		} else {
			LL_RTC_ALMB_Disable(rtcHandle);
			LL_RTC_DisableIT_ALRB(rtcHandle);
			LL_RTC_ClearFlag_ALRB(rtcHandle);
		}
	}


	void EnableAlarmHardware(uint8 index) {
		if (index == 0) {
			LL_RTC_ClearFlag_ALRA(rtcHandle);
			LL_RTC_EnableIT_ALRA(rtcHandle);
			LL_RTC_ALMA_Enable(rtcHandle);
		} else {
			LL_RTC_ClearFlag_ALRB(rtcHandle);
			LL_RTC_EnableIT_ALRB(rtcHandle);
			LL_RTC_ALMB_Enable(rtcHandle);
		}
	}


	bool WaitForAlarmWritable(uint8 index) const {
		uint32 retries = WaitLoopCount;
		while (!IsAlarmWritable(index) && retries > 0) {
			retries--;
		}

		return IsAlarmWritable(index);
	}


	bool IsAlarmWritable(uint8 index) const {
		if (index == 0) {
			return LL_RTC_IsActiveFlag_ALRAW(rtcHandle) != 0;
		}

		return LL_RTC_IsActiveFlag_ALRBW(rtcHandle) != 0;
	}


	bool WaitForWakeupWritable() const {
		uint32 retries = WaitLoopCount;
		while (LL_RTC_IsActiveFlag_WUTW(rtcHandle) == 0 && retries > 0) {
			retries--;
		}

		return LL_RTC_IsActiveFlag_WUTW(rtcHandle) != 0;
	}
};
