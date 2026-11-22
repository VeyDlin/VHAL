#pragma once
// Host scenarios for the EXTI gate of an STM32 GPIO port: the handler serves only an owned, unmasked line in an interrupt
// mode, clears a pending bit it owns, and leaves the bits of other lines and of masked lines alone. A test includes
// GpioExtiSim.h and the port header, then calls RunGpioExtiScenarios<Port>(name). The same scenarios run on every port, so
// the G0, G4 and F4 gates cannot drift apart.
#include <cstdio>


namespace GpioExtiScenarios {


inline int failures = 0;
inline int handlerCalls = 0;
inline const char *currentPort = "";


inline void Check(bool condition, const char *expression, int line) {
	if (!condition) {
		failures++;
		std::fprintf(stderr, "TestGpioExti%s: line %d: check failed: %s\n", currentPort, line, expression);
	}
}


#define GPIO_EXTI_CHECK(condition) Check(static_cast<bool>(condition), #condition, __LINE__)


inline uint32 PendingBits() {
	return simExti.rpr | simExti.fpr;
}


inline void ResetModel() {
	simExti = SimExti();
	handlerCalls = 0;
}


template<typename Port>
class ExtiProbe : public Port {
public:
	ExtiProbe(GPIO_TypeDef *gpioPort, uint8 gpioPin) : Port(gpioPort, gpioPin) { }

	// Sets the mode without the port's SetParameters, the way a mode change looks between the store and the release.
	void ForceMode(typename Port::Mode value) {
		this->parameters.mode = value;
	}
};


// Runs the vector the way the NVIC does: the request is asserted while a pending bit is set and unmasked on the line, and
// the handler is entered again after each return while it stays asserted. The count stops at the limit.
template<typename Port>
int RunVector(ExtiProbe<Port> &gpio, uint32 line, int limit) {
	int entries = 0;
	while (entries < limit && (PendingBits() & simExti.imr & line) != 0) {
		gpio.IrqHandler();
		entries++;
	}
	return entries;
}


template<typename Port>
void ConfigureInterrupt(ExtiProbe<Port> &gpio) {
	gpio.interruptPeripheryInit = []() { return ResultStatus::ok; };
	gpio.SetParameters({ Port::Mode::InterruptRising, Port::Pull::None, Port::Speed::Low });
}


template<typename Port>
void ServesUnmaskedInterruptOnce() {
	ResetModel();
	ExtiProbe<Port> gpio(GPIOA, 0);
	gpio.onInterrupt = [](bool) {
		handlerCalls++;
	};
	ConfigureInterrupt(gpio);

	simExti.rpr = 1;
	int entries = RunVector(gpio, 1, 100);
	GPIO_EXTI_CHECK(entries == 1);
	GPIO_EXTI_CHECK(handlerCalls == 1);
	GPIO_EXTI_CHECK(PendingBits() == 0);
}


template<typename Port>
void MaskedPendingIsKept() {
	ResetModel();
	ExtiProbe<Port> gpio(GPIOA, 0);
	gpio.onInterrupt = [](bool) {
		handlerCalls++;
	};
	ConfigureInterrupt(gpio);

	// The line is masked in EXTI_IMR, so its pending bit is not a request and the handler leaves it for the enable to drop.
	simExti.imr = 0;
	simExti.rpr = 1;
	gpio.IrqHandler();
	GPIO_EXTI_CHECK(handlerCalls == 0);
	GPIO_EXTI_CHECK(simExti.rpr == 1);
}


template<typename Port>
void EnableDropsStalePending() {
	ResetModel();
	ExtiProbe<Port> gpio(GPIOA, 0);
	gpio.onInterrupt = [](bool) {
		handlerCalls++;
	};

	simExti.rpr = 1;
	gpio.IrqHandler();
	GPIO_EXTI_CHECK(handlerCalls == 0);
	GPIO_EXTI_CHECK(simExti.rpr == 1);

	ConfigureInterrupt(gpio);
	GPIO_EXTI_CHECK(simExti.rpr == 0);
	GPIO_EXTI_CHECK(simExti.imr == 1);
}


template<typename Port>
void EventModeKeepsPending() {
	// An event line is masked in EXTI_IMR, so it never raises the vector; the handler serves nothing and keeps its bit.
	ResetModel();
	ExtiProbe<Port> gpio(GPIOA, 0);
	gpio.onInterrupt = [](bool) {
		handlerCalls++;
	};
	gpio.eventPeripheryInit = []() { return ResultStatus::ok; };
	gpio.SetParameters({ Port::Mode::EventRising, Port::Pull::None, Port::Speed::Low });

	simExti.rpr = 1;
	int entries = RunVector(gpio, 1, 100);
	gpio.IrqHandler();
	GPIO_EXTI_CHECK(entries == 0);
	GPIO_EXTI_CHECK(handlerCalls == 0);
	GPIO_EXTI_CHECK(simExti.rpr == 1);
}


template<typename Port>
void UnmaskedPendingLeftByModeIsCleared() {
	// The window between the store of a non-interrupt mode and ReleaseExti(): the line is still unmasked and an edge is
	// pending. The vector must not re-enter for it, so the handler clears the owned pending bit and does not serve it.
	ResetModel();
	ExtiProbe<Port> gpio(GPIOA, 0);
	gpio.onInterrupt = [](bool) {
		handlerCalls++;
	};
	ConfigureInterrupt(gpio);

	simExti.rpr = 1;
	gpio.ForceMode(Port::Mode::Output);

	int entries = RunVector(gpio, 1, 100);
	GPIO_EXTI_CHECK(entries == 1);
	GPIO_EXTI_CHECK(handlerCalls == 0);
	GPIO_EXTI_CHECK(PendingBits() == 0);
}


template<typename Port>
void OtherLinesOnSharedVectorAreUntouched() {
	// Line 1 belongs to port B and is unmasked and pending. The handler of pin 0 of port A must not clear it or serve it.
	ResetModel();
	ExtiProbe<Port> gpio(GPIOA, 0);
	gpio.onInterrupt = [](bool) {
		handlerCalls++;
	};
	ConfigureInterrupt(gpio);

	simExti.source[1] = LL_EXTI_CONFIG_PORTB;
	simExti.imr |= 2;
	simExti.rpr = 2;
	gpio.IrqHandler();
	GPIO_EXTI_CHECK(handlerCalls == 0);
	GPIO_EXTI_CHECK(simExti.rpr == 2);
}


template<typename Port>
int RunGpioExtiScenarios(const char *name) {
	currentPort = name;
	failures = 0;

	ServesUnmaskedInterruptOnce<Port>();
	MaskedPendingIsKept<Port>();
	EnableDropsStalePending<Port>();
	EventModeKeepsPending<Port>();
	UnmaskedPendingLeftByModeIsCleared<Port>();
	OtherLinesOnSharedVectorAreUntouched<Port>();

	if (failures != 0) {
		std::fprintf(stderr, "TestGpioExti%s: %d check(s) failed\n", name, failures);
		return 1;
	}

	std::printf("TestGpioExti%s: all checks passed\n", name);
	return 0;
}


}
