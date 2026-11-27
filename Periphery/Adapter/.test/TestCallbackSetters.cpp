#include <VHAL.h>
#include <Adapter/ADCAdapter.h>
#include <Adapter/CANAdapter.h>
#include <Adapter/COMPAdapter.h>
#include <Adapter/DACAdapter.h>
#include <Adapter/DMAAdapter.h>
#include <Adapter/GPIOAdapter.h>
#include <Adapter/I2CAdapter.h>
#include <Adapter/I2SAdapter.h>
#include <Adapter/PowerAdapter.h>
#include <Adapter/RTCAdapter.h>
#include <Adapter/SPIAdapter.h>
#include <Adapter/TIMAdapter.h>
#include <Adapter/UARTAdapter.h>
#include <Adapter/USBDeviceAdapter.h>
#include <Adapter/Port/ENS/Adapter/WaveGeneratorAdapter.h>
#include <Adapter/Port/ESP32/Adapter/GPTimerAdapter.h>
#include <Adapter/Port/ESP32/Adapter/LEDCAdapter.h>
#include <cstdio>

// Host tests for the Set-setters of the callback fields. A section is counted by the shim: simCriticalDepth is the number of
// open sections, and simCriticalHook runs at each enter and leave. The tests check three rules: the field is written inside
// the section, the old callable is destroyed after the section is left, and a setter nests inside a section that is already
// open. A delegate setter is plain assignment inside the section.


namespace {


int failureCount = 0;


void ExpectEqual(long long actual, long long expected, const char* expression, int line) {
	if (actual != expected) {
		failureCount++;
		std::fprintf(stderr, "TestCallbackSetters.cpp:%d: %s is %lld, expected %lld\n", line, expression, actual, expected);
	}
}


void ExpectTrue(bool condition, const char* expression, int line) {
	if (!condition) {
		failureCount++;
		std::fprintf(stderr, "TestCallbackSetters.cpp:%d: check failed: %s\n", line, expression);
	}
}


#define SETTER_EQ(actual, expected) ExpectEqual(static_cast<long long>(actual), static_cast<long long>(expected), #actual, __LINE__)
#define SETTER_TRUE(condition) ExpectTrue((condition), #condition, __LINE__)


// A callable that stores its id in lastId when it runs.
struct Recorder {
	int id;
	int* lastId;

	void operator()() const {
		*lastId = id;
	}
};


// A callable that counts its own destruction and the section depth at that moment. Only the object that owns the state
// counts: a move passes the ownership on, so a moved-from copy does not count.
struct DestroyProbe {
	int* destroyCount;
	int* depthAtDestroy;
	bool owns = true;

	DestroyProbe(int* count, int* depth)
		: destroyCount(count), depthAtDestroy(depth) { }

	DestroyProbe(const DestroyProbe& other)
		: destroyCount(other.destroyCount), depthAtDestroy(other.depthAtDestroy) { }

	DestroyProbe(DestroyProbe&& other) noexcept
		: destroyCount(other.destroyCount), depthAtDestroy(other.depthAtDestroy) {
		other.owns = false;
	}

	void operator()() const { }

	~DestroyProbe() {
		if (owns) {
			(*destroyCount)++;
			*depthAtDestroy = simCriticalDepth;
		}
	}
};


// A DMA adapter with no registers, as in TestDMAAdapter.cpp. Only the hooks the base needs are defined.
class FakeDMA : public DMAAdapter<int> {
public:
	FakeDMA() { }

	virtual void IrqHandler() override { }

	virtual ResultStatus GetStatus() override {
		return ResultStatus::ok;
	}

	virtual ResultStatus Stop() override {
		return ResultStatus::ok;
	}

protected:
	virtual ResultStatus StartTransfer(const uint8*, uint8*, uint32) override {
		return ResultStatus::ok;
	}

	virtual ResultStatus Initialization() override {
		return ResultStatus::ok;
	}
};


// A USB device adapter with no hardware. The stubs do nothing; the tests only use the event delegates.
class FakeUSB : public USBDeviceAdapter {
public:
	virtual ResultStatus Connect() override {
		return ResultStatus::ok;
	}

	virtual ResultStatus Disconnect() override {
		return ResultStatus::ok;
	}

	virtual bool IsConnected() const override {
		return false;
	}

	virtual ResultStatus SetAddress(uint8) override {
		return ResultStatus::ok;
	}

	virtual ResultStatus OpenEndpoint(const EndpointConfig&) override {
		return ResultStatus::ok;
	}

	virtual ResultStatus CloseEndpoint(uint8) override {
		return ResultStatus::ok;
	}

	virtual void CloseAllEndpoints() override { }

	virtual ResultStatus StartInTransfer(uint8, std::span<const uint8>, bool) override {
		return ResultStatus::ok;
	}

	virtual ResultStatus StartOutTransfer(uint8, std::span<uint8>) override {
		return ResultStatus::ok;
	}

	virtual ResultStatus Stall(uint8) override {
		return ResultStatus::ok;
	}

	virtual ResultStatus ClearStall(uint8) override {
		return ResultStatus::ok;
	}

	virtual bool IsStalled(uint8) const override {
		return false;
	}

	virtual ResultStatus RemoteWakeup() override {
		return ResultStatus::ok;
	}

	virtual uint16 GetFrameNumber() const override {
		return 0;
	}

	virtual void IrqHandler() override { }

	virtual ResultStatus Initialization() override {
		return ResultStatus::ok;
	}
};


// The name of the delegate that ran last: 'A' or 'B', or '-' when none ran.
char lastBusReset = '-';


void RecordBusResetA(void*, USBSpeed) {
	lastBusReset = 'A';
}


void RecordBusResetB(void*, USBSpeed) {
	lastBusReset = 'B';
}


// Compile check: taking the address of a member function instantiates its definition. Every setter of every base the shim
// compiles is named here. The bases with a template parameter are instantiated with int, as the other tests do.
void InstantiateEverySetter() {
	(void)&ADCAdapter<int>::SetOnInterrupt;
	(void)&ADCAdapter<int>::SetOnError;

	(void)&CANAdapter<int>::SetOnReceive;
	(void)&CANAdapter<int>::SetOnTransmitComplete;
	(void)&CANAdapter<int>::SetOnBusStateChanged;
	(void)&CANAdapter<int>::SetOnError;

	(void)&COMPAdapter<int>::SetOnInterrupt;

	(void)&DACAdapter<int>::SetOnError;

	(void)&DMAAdapter<int>::SetOnTransferComplete;
	(void)&DMAAdapter<int>::SetOnHalfTransfer;
	(void)&DMAAdapter<int>::SetOnError;

	(void)&GPIOAdapter<>::SetOnInterrupt;

	(void)&I2CAdapter<int>::SetOnSlaveRead;
	(void)&I2CAdapter<int>::SetOnSlaveWrite;
	(void)&I2CAdapter<int>::SetOnSlaveEndTransfer;
	(void)&I2CAdapter<int>::SetOnError;
	(void)&I2CAdapter<int>::SetOnComplete;
	(void)&I2CAdapter<int>::SetOnSlaveAddressMatch;
	(void)&I2CAdapter<int>::SetOnSlaveStop;

	(void)&I2SAdapter<int>::SetOnInterrupt;
	(void)&I2SAdapter<int>::SetOnError;

	(void)&PowerAdapter<int>::SetOnVoltageChange;

	(void)&RTCAdapter<int>::SetOnAlarm;
	(void)&RTCAdapter<int>::SetOnWakeup;

	(void)&SPIAdapter<int>::SetOnInterrupt;
	(void)&SPIAdapter<int>::SetOnError;

	(void)&TIMAdapter<int>::SetOnInputCaptureEvent;
	(void)&TIMAdapter<int>::SetOnOutputCompareEvent;
	(void)&TIMAdapter<int>::SetOnUpdateEvent;
	(void)&TIMAdapter<int>::SetOnBreakInputEvent;
	(void)&TIMAdapter<int>::SetOnBreak2InputEvent;
	(void)&TIMAdapter<int>::SetOnTriggerDetectionEvent;
	(void)&TIMAdapter<int>::SetOnCommutationEvent;

	(void)&UARTAdapter<int>::SetOnInterrupt;
	(void)&UARTAdapter<int>::SetOnError;

	(void)&FakeUSB::SetOnBusReset;
	(void)&FakeUSB::SetOnSetup;
	(void)&FakeUSB::SetOnTransferComplete;
	(void)&FakeUSB::SetOnSuspend;
	(void)&FakeUSB::SetOnResume;
	(void)&FakeUSB::SetOnStartOfFrame;
	(void)&FakeUSB::SetOnError;

	(void)&WaveGeneratorAdapter<int>::SetOnInterrupt;

	(void)&GPTimerAdapter::SetOnAlarm;

	(void)&LEDCAdapter::SetOnFadeEnd;
}


// The field holds the old callable when the setter enters the section and the new one when it leaves. Both boundaries run
// inside the section, at depth 1.
void TestFieldChangesInsideSection() {
	int lastId = 0;
	FakeDMA dma;
	dma.SetOnTransferComplete(Recorder{ 1, &lastId });

	int idAtEnter = -1;
	int idAtLeave = -1;
	int depthAtEnter = -1;
	int depthAtLeave = -1;
	simCriticalHook = [&](bool isEnable) {
		lastId = 0;
		dma.onTransferComplete();
		if (isEnable) {
			idAtEnter = lastId;
			depthAtEnter = simCriticalDepth;
		} else {
			idAtLeave = lastId;
			depthAtLeave = simCriticalDepth;
		}
	};

	dma.SetOnTransferComplete(Recorder{ 2, &lastId });
	simCriticalHook = nullptr;

	SETTER_EQ(idAtEnter, 1);
	SETTER_EQ(idAtLeave, 2);
	SETTER_EQ(depthAtEnter, 1);
	SETTER_EQ(depthAtLeave, 1);
	SETTER_EQ(simCriticalDepth, 0);

	lastId = 0;
	dma.onTransferComplete();
	SETTER_EQ(lastId, 2);
}


// The old callable is destroyed once, after the section is left, so its destructor sees depth 0.
void TestOldCallableDestroyedAfterLeave() {
	int destroyCount = 0;
	int depthAtDestroy = -1;
	FakeDMA dma;

	dma.SetOnTransferComplete(DestroyProbe(&destroyCount, &depthAtDestroy));
	SETTER_EQ(destroyCount, 0);

	int lastId = 0;
	dma.SetOnTransferComplete(Recorder{ 4, &lastId });
	SETTER_EQ(destroyCount, 1);
	SETTER_EQ(depthAtDestroy, 0);
}


// A setter called while the caller's section is open nests: the depth comes back to the caller's level, and the old callable
// is destroyed at that level, because the outer section is still open.
void TestNestedInsideOpenSection() {
	int destroyCount = 0;
	int depthAtDestroy = -1;
	FakeDMA dma;
	dma.SetOnTransferComplete(DestroyProbe(&destroyCount, &depthAtDestroy));

	int lastId = 0;
	uint32 entersBefore = simCriticalEnters;
	System::CriticalSection(true);
	dma.SetOnTransferComplete(Recorder{ 5, &lastId });

	SETTER_EQ(simCriticalDepth, 1);
	SETTER_EQ(simCriticalEnters - entersBefore, 2);
	SETTER_EQ(destroyCount, 1);
	SETTER_EQ(depthAtDestroy, 1);

	dma.onTransferComplete();
	SETTER_EQ(lastId, 5);

	System::CriticalSection(false);
	SETTER_EQ(simCriticalDepth, 0);
}


// A setter with a null callable clears the field. The old callable is still destroyed after the section.
void TestSetterWithEmptyCallable() {
	int destroyCount = 0;
	int depthAtDestroy = -1;
	FakeDMA dma;
	dma.SetOnError(std::function<void()>(DestroyProbe(&destroyCount, &depthAtDestroy)));

	dma.SetOnError(nullptr);
	SETTER_TRUE(!dma.onError);
	SETTER_EQ(destroyCount, 1);
	SETTER_EQ(depthAtDestroy, 0);
}


// The delegate setter assigns inside one section. The field holds the old delegate at enter and the new one at leave.
void TestDelegateSetter() {
	FakeUSB usb;
	SETTER_TRUE(!usb.onBusReset);

	usb.SetOnBusReset(Callback::Delegate<void(USBSpeed speed)>::FromRaw(RecordBusResetA));
	lastBusReset = '-';
	usb.onBusReset(static_cast<USBSpeed>(1));
	SETTER_TRUE(lastBusReset == 'A');

	char seenAtEnter = '-';
	char seenAtLeave = '-';
	int depthAtEnter = -1;
	int depthAtLeave = -1;
	simCriticalHook = [&](bool isEnable) {
		lastBusReset = '-';
		usb.onBusReset(static_cast<USBSpeed>(0));
		if (isEnable) {
			seenAtEnter = lastBusReset;
			depthAtEnter = simCriticalDepth;
		} else {
			seenAtLeave = lastBusReset;
			depthAtLeave = simCriticalDepth;
		}
	};

	uint32 entersBefore = simCriticalEnters;
	usb.SetOnBusReset(Callback::Delegate<void(USBSpeed speed)>::FromRaw(RecordBusResetB));
	simCriticalHook = nullptr;

	SETTER_EQ(simCriticalEnters - entersBefore, 1);
	SETTER_TRUE(seenAtEnter == 'A');
	SETTER_TRUE(seenAtLeave == 'B');
	SETTER_EQ(depthAtEnter, 1);
	SETTER_EQ(depthAtLeave, 1);
	SETTER_EQ(simCriticalDepth, 0);

	lastBusReset = '-';
	usb.onBusReset(static_cast<USBSpeed>(2));
	SETTER_TRUE(lastBusReset == 'B');
}


}


int main() {
	InstantiateEverySetter();
	TestFieldChangesInsideSection();
	TestOldCallableDestroyedAfterLeave();
	TestNestedInsideOpenSection();
	TestSetterWithEmptyCallable();
	TestDelegateSetter();

	if (failureCount != 0) {
		std::fprintf(stderr, "TestCallbackSetters: %d check(s) failed\n", failureCount);
		return 1;
	}

	std::printf("TestCallbackSetters: all checks passed\n");
	return 0;
}
