#include <VHAL.h>
#include <Adapter/GPIOAdapter.h>
#include <cstdio>

// Host tests for the EXTI rules that the GPIO base class holds: the pin index that selects the SYSCFG or EXTI port field,
// and the edges and the interrupt or event kind that a mode selects. The register writes of each port are not modelled here.


namespace {


int failureCount = 0;


void Expect(bool condition, const char* expression, int line) {
	if (!condition) {
		failureCount++;
		std::fprintf(stderr, "TestGPIOAdapter.cpp:%d: check failed: %s\n", line, expression);
	}
}


void ExpectEqual(long long actual, long long expected, const char* expression, int line) {
	if (actual != expected) {
		failureCount++;
		std::fprintf(stderr, "TestGPIOAdapter.cpp:%d: %s is %lld, expected %lld\n", line, expression, actual, expected);
	}
}


#define GP_EXPECT(condition) Expect(static_cast<bool>(condition), #condition, __LINE__)
#define GP_EQ(actual, expected) ExpectEqual(static_cast<long long>(actual), static_cast<long long>(expected), #actual, __LINE__)


struct FakePort {
	uint32 unused = 0;
};


using Base = GPIOAdapter<FakePort>;


// The rules are protected in the base, so the tests reach them through a derived class.
struct GpioProbe : Base {
	using Base::PinIndex;
	using Base::IsInterruptMode;
	using Base::IsEventMode;
	using Base::HasRisingEdge;
	using Base::HasFallingEdge;
	using Base::CanTakeExtiLine;
	using Base::ShouldReleaseExtiLine;
};


using Gpio = GpioProbe;
using Mode = Gpio::Mode;


void TestPinIndex() {
	for (uint32 index = 0; index < 16; index++) {
		GP_EQ(Gpio::PinIndex(1u << index), index);
	}

	GP_EQ(Gpio::PinIndex(1u << 31), 31);
}


void TestExtiLineTaking() {
	// A free line can be taken, and so can a line this port already owns.
	GP_EXPECT(Gpio::CanTakeExtiLine(false, false));
	GP_EXPECT(Gpio::CanTakeExtiLine(true, true));

	// A line in use by another port cannot be taken.
	GP_EXPECT(!Gpio::CanTakeExtiLine(true, false));
}


void TestExtiLineRelease() {
	// Only a line that is in use and owned by this port is released.
	GP_EXPECT(Gpio::ShouldReleaseExtiLine(true, true));

	GP_EXPECT(!Gpio::ShouldReleaseExtiLine(true, false));
	GP_EXPECT(!Gpio::ShouldReleaseExtiLine(false, false));
	GP_EXPECT(!Gpio::ShouldReleaseExtiLine(false, true));
}


void TestEdges() {
	// Rising and falling edge of every mode that has an EXTI interrupt or event.
	GP_EXPECT(Gpio::HasRisingEdge(Mode::InterruptRising));
	GP_EXPECT(!Gpio::HasFallingEdge(Mode::InterruptRising));

	GP_EXPECT(!Gpio::HasRisingEdge(Mode::InterruptFalling));
	GP_EXPECT(Gpio::HasFallingEdge(Mode::InterruptFalling));

	GP_EXPECT(Gpio::HasRisingEdge(Mode::InterruptRisingFalling));
	GP_EXPECT(Gpio::HasFallingEdge(Mode::InterruptRisingFalling));

	GP_EXPECT(Gpio::HasRisingEdge(Mode::EventRising));
	GP_EXPECT(!Gpio::HasFallingEdge(Mode::EventRising));

	GP_EXPECT(!Gpio::HasRisingEdge(Mode::EventFalling));
	GP_EXPECT(Gpio::HasFallingEdge(Mode::EventFalling));

	GP_EXPECT(Gpio::HasRisingEdge(Mode::EventRisingFalling));
	GP_EXPECT(Gpio::HasFallingEdge(Mode::EventRisingFalling));

	// Modes without an EXTI line select no edge.
	Mode plain[] = { Mode::Input, Mode::Output, Mode::OpenDrain, Mode::Alternate, Mode::AlternateOpenDrain, Mode::Analog };
	for (Mode value : plain) {
		GP_EXPECT(!Gpio::HasRisingEdge(value));
		GP_EXPECT(!Gpio::HasFallingEdge(value));
		GP_EXPECT(!Gpio::IsInterruptMode(value));
		GP_EXPECT(!Gpio::IsEventMode(value));
	}
}


void TestInterruptAndEventKinds() {
	GP_EXPECT(Gpio::IsInterruptMode(Mode::InterruptRising));
	GP_EXPECT(Gpio::IsInterruptMode(Mode::InterruptFalling));
	GP_EXPECT(Gpio::IsInterruptMode(Mode::InterruptRisingFalling));
	GP_EXPECT(!Gpio::IsEventMode(Mode::InterruptRising));

	GP_EXPECT(Gpio::IsEventMode(Mode::EventRising));
	GP_EXPECT(Gpio::IsEventMode(Mode::EventFalling));
	GP_EXPECT(Gpio::IsEventMode(Mode::EventRisingFalling));
	GP_EXPECT(!Gpio::IsInterruptMode(Mode::EventRising));
}


// A pin that the port does not gate itself (ESP32 and ENS001) gets its callback from the base handler, and only in an
// interrupt mode. The state the callback receives is the pin state.
int irqCalls = 0;
bool irqLevel = false;


class IrqGpio : public Base {
public:
	explicit IrqGpio(FakePort *gpioPort) : Base(gpioPort, 3, false) { }

	void SetMode(Mode mode) {
		parameters.mode = mode;
	}


protected:
	ResultStatus Initialization() override {
		return ResultStatus::ok;
	}


	bool GetPinState() override {
		return true;
	}


	void SetPinState(bool) override { }

	void TogglePinState() override { }
};


void TestIrqHandlerServesInterruptModesOnly() {
	FakePort port;
	IrqGpio gpio(&port);
	gpio.onInterrupt = [](bool state) {
		irqCalls++;
		irqLevel = state;
	};

	Mode notInterrupt[] = {
		Mode::Input, Mode::Output, Mode::OpenDrain, Mode::EventRising, Mode::EventFalling, Mode::EventRisingFalling,
		Mode::Alternate, Mode::AlternateOpenDrain, Mode::Analog
	};
	for (Mode value : notInterrupt) {
		gpio.SetMode(value);
		irqCalls = 0;
		gpio.IrqHandler();
		GP_EQ(irqCalls, 0);
	}

	Mode interrupts[] = { Mode::InterruptRising, Mode::InterruptFalling, Mode::InterruptRisingFalling };
	for (Mode value : interrupts) {
		gpio.SetMode(value);
		irqCalls = 0;
		irqLevel = false;
		gpio.IrqHandler();
		GP_EQ(irqCalls, 1);
		GP_EXPECT(irqLevel);
	}
}


}


int main() {
	TestPinIndex();
	TestEdges();
	TestInterruptAndEventKinds();
	TestExtiLineTaking();
	TestExtiLineRelease();
	TestIrqHandlerServesInterruptModesOnly();

	if (failureCount != 0) {
		std::fprintf(stderr, "TestGPIOAdapter: %d check(s) failed\n", failureCount);
		return 1;
	}

	std::printf("TestGPIOAdapter: all checks passed\n");
	return 0;
}
