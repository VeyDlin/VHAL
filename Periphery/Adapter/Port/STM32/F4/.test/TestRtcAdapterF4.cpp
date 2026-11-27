#include <VHAL.h>
#include <Stm32F4RtcSim.h>
#include <Adapter/Port/STM32/F4/RTCAdapterF4.h>
#include <cstdio>


// Host tests for the F4 RTC interrupt handler: the alarm and wakeup flags are cleared whether or not their interrupt is
// enabled, the callbacks run only for an enabled source, and the EXTI pending bits of lines 17 and 22 are cleared on every
// entry, so a source disarmed between the edge and the handler does not leave the vector re-entering. The register model is
// in shim/Stm32F4RtcSim.h.


namespace {


int failureCount = 0;
int alarmCalls = 0;
uint8 lastAlarmIndex = 0xFF;
int wakeupCalls = 0;


void ExpectEqual(unsigned long long actual, unsigned long long expected, const char* expression, int line) {
	if (actual != expected) {
		failureCount++;
		std::fprintf(stderr, "TestRtcAdapterF4.cpp:%d: check failed: %s (actual %llu, expected %llu)\n", line, expression, actual, expected);
	}
}


#define EXPECT_EQ(actual, expected) ExpectEqual(static_cast<unsigned long long>(actual), static_cast<unsigned long long>(expected), #actual " == " #expected, __LINE__)


void ResetCase() {
	simExtiPending = 0;
	alarmCalls = 0;
	lastAlarmIndex = 0xFF;
	wakeupCalls = 0;
}


void WireCallbacks(RTCAdapterF4 &adapter) {
	adapter.SetOnAlarm([](uint8 alarmIndex) {
		alarmCalls++;
		lastAlarmIndex = alarmIndex;
	});
	adapter.SetOnWakeup([]() {
		wakeupCalls++;
	});
}


void TestEnabledAlarmIsServedOnce() {
	ResetCase();
	RTC_TypeDef regs;
	regs.ISR = RTC_ISR_ALRAF;
	regs.CR = RTC_CR_ALRAIE;
	simExtiPending = LL_EXTI_LINE_17;

	RTCAdapterF4 rtc(&regs, 32768);
	WireCallbacks(rtc);
	rtc.IrqHandler();

	EXPECT_EQ(alarmCalls, 1);
	EXPECT_EQ(lastAlarmIndex, 0);
	EXPECT_EQ(regs.ISR & RTC_ISR_ALRAF, 0);
	EXPECT_EQ(simExtiPending & LL_EXTI_LINE_17, 0);
}


void TestDisabledAlarmFlagIsClearedWithoutCallback() {
	// The alarm was raised, but its interrupt is off. The flag is cleared and the callback does not run.
	ResetCase();
	RTC_TypeDef regs;
	regs.ISR = RTC_ISR_ALRAF;
	regs.CR = 0;
	simExtiPending = LL_EXTI_LINE_17;

	RTCAdapterF4 rtc(&regs, 32768);
	WireCallbacks(rtc);
	rtc.IrqHandler();

	EXPECT_EQ(alarmCalls, 0);
	EXPECT_EQ(regs.ISR & RTC_ISR_ALRAF, 0);
	EXPECT_EQ(simExtiPending & LL_EXTI_LINE_17, 0);
}


void TestDisarmedAlarmClearsExtiPending() {
	// The alarm was disarmed after the EXTI edge: no flag is set and no interrupt is enabled. The vector must not keep
	// re-entering for the latched line, so the handler clears the EXTI pending bit anyway.
	ResetCase();
	RTC_TypeDef regs;
	regs.ISR = 0;
	regs.CR = 0;
	simExtiPending = LL_EXTI_LINE_17;

	RTCAdapterF4 rtc(&regs, 32768);
	WireCallbacks(rtc);
	rtc.IrqHandler();

	EXPECT_EQ(alarmCalls, 0);
	EXPECT_EQ(simExtiPending & LL_EXTI_LINE_17, 0);
}


void TestEnabledAlarmBIsServedWithIndexOne() {
	ResetCase();
	RTC_TypeDef regs;
	regs.ISR = RTC_ISR_ALRBF;
	regs.CR = RTC_CR_ALRBIE;
	simExtiPending = LL_EXTI_LINE_17;

	RTCAdapterF4 rtc(&regs, 32768);
	WireCallbacks(rtc);
	rtc.IrqHandler();

	EXPECT_EQ(alarmCalls, 1);
	EXPECT_EQ(lastAlarmIndex, 1);
	EXPECT_EQ(regs.ISR & RTC_ISR_ALRBF, 0);
	EXPECT_EQ(simExtiPending & LL_EXTI_LINE_17, 0);
}


void TestEnabledWakeupIsServedOnce() {
	ResetCase();
	RTC_TypeDef regs;
	regs.ISR = RTC_ISR_WUTF;
	regs.CR = RTC_CR_WUTIE;
	simExtiPending = LL_EXTI_LINE_22;

	RTCAdapterF4 rtc(&regs, 32768);
	WireCallbacks(rtc);
	rtc.IrqHandler();

	EXPECT_EQ(wakeupCalls, 1);
	EXPECT_EQ(regs.ISR & RTC_ISR_WUTF, 0);
	EXPECT_EQ(simExtiPending & LL_EXTI_LINE_22, 0);
}


void TestDisarmedWakeupClearsExtiPending() {
	ResetCase();
	RTC_TypeDef regs;
	regs.ISR = 0;
	regs.CR = 0;
	simExtiPending = LL_EXTI_LINE_22;

	RTCAdapterF4 rtc(&regs, 32768);
	WireCallbacks(rtc);
	rtc.IrqHandler();

	EXPECT_EQ(wakeupCalls, 0);
	EXPECT_EQ(simExtiPending & LL_EXTI_LINE_22, 0);
}


void TestWakeupFlagWithInterruptOffIsClearedWithoutCallback() {
	ResetCase();
	RTC_TypeDef regs;
	regs.ISR = RTC_ISR_WUTF;
	regs.CR = 0;
	simExtiPending = LL_EXTI_LINE_22;

	RTCAdapterF4 rtc(&regs, 32768);
	WireCallbacks(rtc);
	rtc.IrqHandler();

	EXPECT_EQ(wakeupCalls, 0);
	EXPECT_EQ(regs.ISR & RTC_ISR_WUTF, 0);
	EXPECT_EQ(simExtiPending & LL_EXTI_LINE_22, 0);
}


}


int main() {
	TestEnabledAlarmIsServedOnce();
	TestDisabledAlarmFlagIsClearedWithoutCallback();
	TestDisarmedAlarmClearsExtiPending();
	TestEnabledAlarmBIsServedWithIndexOne();
	TestEnabledWakeupIsServedOnce();
	TestDisarmedWakeupClearsExtiPending();
	TestWakeupFlagWithInterruptOffIsClearedWithoutCallback();

	if (failureCount != 0) {
		std::fprintf(stderr, "TestRtcAdapterF4: %d check(s) failed\n", failureCount);
		return 1;
	}

	std::printf("TestRtcAdapterF4: all checks passed\n");
	return 0;
}
