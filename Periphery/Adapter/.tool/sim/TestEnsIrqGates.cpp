#include <VHAL.h>
#include <cstdio>


// Host tests for the ENS001 interrupt gates. The comparator has no interrupt-enable bit, so a software flag gates its
// callback. The wave generator clears its flags with a write that takes the enable bit from the shadow register, and its
// callback runs only while the shadow has the enable bit set.
//
// The register models below hold the fields and the bit values of the ENS device header
// (Port/ENS/001/Drivers/Device/CMSDK_CM0.h) that these two adapters reach. The device header itself includes the CMSIS core
// headers, which the host build does not have, so the models are written out here.


struct CMSDK_ANAC_TypeDef {
	volatile uint32 COMP0_CTRL;
	volatile uint32 COMP1_CTRL;
};


struct CMSDK_WAVE_GEN_TypeDef {
	volatile uint32 WAVE_GEN_DRV_CONFIG_REG;
	volatile uint32 WAVE_GEN_DRV_CTRL_REG;
	volatile uint32 WAVE_GEN_DRV_REST_T_REG;
	volatile uint32 WAVE_GEN_DRV_SILENT_T_REG;
	volatile uint32 WAVE_GEN_DRV_HLF_WAVE_PRD_REG;
	volatile uint32 WAVE_GEN_DRV_NEG_HLF_WAVE_PRD_REG;
	volatile uint32 WAVE_GEN_DRV_CLK_FREQ_REG;
	volatile uint32 WAVE_GEN_DRV_IN_WAVE_ADDR_REG;
	volatile uint32 WAVE_GEN_DRV_IN_WAVE_REG;
	volatile uint32 WAVE_GEN_DRV_ALT_LIM_REG;
	volatile uint32 WAVE_GEN_DRV_ALT_SILENT_LIM_REG;
	volatile uint32 WAVE_GEN_DRV_DELAY_LIM_REG;
	volatile uint32 WAVE_GEN_DRV_NEG_SCALE_REG;
	volatile uint32 WAVE_GEN_DRV_NEG_OFFSET_REG;
	volatile uint32 WAVE_GEN_DRV_INT_REG;
	volatile uint32 WAVE_GEN_DRV_ISEL_REG;
	volatile uint32 WAVE_GEN_DRV_SW_CONFIG_REG;
};


struct CMSDK_SYSCON_TypeDef {
	volatile uint32 APB_CLKEN;
};


// The clock enable is only used by Initialization(), which these tests do not call.
inline CMSDK_SYSCON_TypeDef simSyscon;
#define CMSDK_SYSCON (&simSyscon)


#define CMSDK_WAVE_GEN_DRV_INT_EN_Pos                 0
#define CMSDK_WAVE_GEN_DRV_INT_EN_Msk                 (0x01ul << CMSDK_WAVE_GEN_DRV_INT_EN_Pos)
#define CMSDK_WAVE_GEN_DRV_INT_FIRSTADDR_CLR_Pos      1
#define CMSDK_WAVE_GEN_DRV_INT_FIRSTADDR_CLR_Msk      (0x01ul << CMSDK_WAVE_GEN_DRV_INT_FIRSTADDR_CLR_Pos)
#define CMSDK_WAVE_GEN_DRV_INT_SECONDADDR_CLR_Pos     2
#define CMSDK_WAVE_GEN_DRV_INT_SECONDADDR_CLR_Msk     (0x01ul << CMSDK_WAVE_GEN_DRV_INT_SECONDADDR_CLR_Pos)
#define CMSDK_WAVE_GEN_DRV_INT_FIRSTADDR_STS_Pos      9
#define CMSDK_WAVE_GEN_DRV_INT_FIRSTADDR_STS_Msk      (0x01ul << CMSDK_WAVE_GEN_DRV_INT_FIRSTADDR_STS_Pos)
#define CMSDK_WAVE_GEN_DRV_INT_SECONDADDR_STS_Pos     10
#define CMSDK_WAVE_GEN_DRV_INT_SECONDADDR_STS_Msk     (0x01ul << CMSDK_WAVE_GEN_DRV_INT_SECONDADDR_STS_Pos)
#define CMSDK_WAVE_GEN_DRV_INT_FIRST_ADDR_Pos         8
#define CMSDK_WAVE_GEN_DRV_INT_FIRST_ADDR_Msk         (0xFFul << CMSDK_WAVE_GEN_DRV_INT_FIRST_ADDR_Pos)
#define CMSDK_WAVE_GEN_DRV_INT_SECOND_ADDR_Pos        16
#define CMSDK_WAVE_GEN_DRV_INT_SECOND_ADDR_Msk        (0xFFul << CMSDK_WAVE_GEN_DRV_INT_SECOND_ADDR_Pos)
#define CMSDK_WAVE_GEN_DRV_INT_READ_FIRST_ADDR_Pos    16
#define CMSDK_WAVE_GEN_DRV_INT_READ_FIRST_ADDR_Msk    (0xFFul << CMSDK_WAVE_GEN_DRV_INT_READ_FIRST_ADDR_Pos)
#define CMSDK_WAVE_GEN_DRV_INT_READ_SECOND_ADDR_Pos   24
#define CMSDK_WAVE_GEN_DRV_INT_READ_SECOND_ADDR_Msk   (0xFFul << CMSDK_WAVE_GEN_DRV_INT_READ_SECOND_ADDR_Pos)


#include <Adapter/Port/ENS/001/COMPAdapter001.h>
#include <Adapter/Port/ENS/001/WaveGeneratorAdapter001.h>


namespace {


int failureCount = 0;
int comparatorCalls = 0;
int waveCalls = 0;
bool lastFirst = false;
bool lastSecond = false;
uint8 lastReadFirst = 0;


void Expect(bool condition, const char* expression, int line) {
	if (!condition) {
		failureCount++;
		std::fprintf(stderr, "TestEnsIrqGates.cpp:%d: check failed: %s\n", line, expression);
	}
}


void ExpectEqual(long long actual, long long expected, const char* expression, int line) {
	if (actual != expected) {
		failureCount++;
		std::fprintf(stderr, "TestEnsIrqGates.cpp:%d: %s is %lld, expected %lld\n", line, expression, actual, expected);
	}
}


#define ENS_EXPECT(condition) Expect(static_cast<bool>(condition), #condition, __LINE__)
#define ENS_EQ(actual, expected) ExpectEqual(static_cast<long long>(actual), static_cast<long long>(expected), #actual, __LINE__)


void TestComparatorWithoutTriggerIsSilent() {
	CMSDK_ANAC_TypeDef anac = {};
	COMPAdapter001 comparator(&anac, COMPAdapter001::Instance::Comparator0);

	comparatorCalls = 0;
	comparator.SetOnInterrupt([](bool) {
		comparatorCalls++;
	});

	comparator.IrqHandler();
	ENS_EQ(comparatorCalls, 0);
}


void TestComparatorRejectsInterruptTriggers() {
	// The comparator cannot enable an interrupt, so each interrupt trigger mode is refused and the callback stays silent.
	CMSDK_ANAC_TypeDef anac = {};
	COMPAdapter001 comparator(&anac, COMPAdapter001::Instance::Comparator1);

	comparatorCalls = 0;
	comparator.SetOnInterrupt([](bool) {
		comparatorCalls++;
	});

	COMPAdapter001::TriggerMode interruptModes[] = {
		COMPAdapter001::TriggerMode::InterruptRising,
		COMPAdapter001::TriggerMode::InterruptFalling,
		COMPAdapter001::TriggerMode::InterruptRisingFalling
	};

	for (COMPAdapter001::TriggerMode mode : interruptModes) {
		ENS_EXPECT(comparator.SetTriggerMode(mode) == ResultStatus::notSupported);
		comparator.IrqHandler();
	}

	ENS_EQ(comparatorCalls, 0);
}


void TestComparatorNoneKeepsInterruptOff() {
	CMSDK_ANAC_TypeDef anac = {};
	COMPAdapter001 comparator(&anac, COMPAdapter001::Instance::Comparator0);

	comparatorCalls = 0;
	comparator.SetOnInterrupt([](bool) {
		comparatorCalls++;
	});

	ENS_EXPECT(comparator.SetTriggerMode(COMPAdapter001::TriggerMode::None) == ResultStatus::ok);
	comparator.IrqHandler();
	ENS_EQ(comparatorCalls, 0);
}


void TestWaveGeneratorServesWhenEnabled() {
	CMSDK_WAVE_GEN_TypeDef waveGen = {};
	WaveGeneratorAdapter001 wave(&waveGen);

	waveCalls = 0;
	wave.SetOnInterrupt([](auto event) {
		waveCalls++;
		lastFirst = event.firstAddress;
		lastSecond = event.secondAddress;
		lastReadFirst = event.readFirstAddr;
	});

	wave.EnableInterrupt(true);
	ENS_EQ(waveGen.WAVE_GEN_DRV_INT_REG, CMSDK_WAVE_GEN_DRV_INT_EN_Msk);

	// The hardware raises the first-address flag with the interrupt enabled. The read-back of the first address is 0x2A.
	waveGen.WAVE_GEN_DRV_INT_REG = CMSDK_WAVE_GEN_DRV_INT_EN_Msk
		| CMSDK_WAVE_GEN_DRV_INT_FIRSTADDR_STS_Msk
		| (0x2Au << CMSDK_WAVE_GEN_DRV_INT_READ_FIRST_ADDR_Pos);

	wave.IrqHandler();

	ENS_EQ(waveCalls, 1);
	ENS_EXPECT(lastFirst);
	ENS_EXPECT(!lastSecond);
	ENS_EQ(lastReadFirst, 0x2A);

	// Both clear bits are written, and the enable bit is written as the adapter set it.
	ENS_EQ(waveGen.WAVE_GEN_DRV_INT_REG,
		CMSDK_WAVE_GEN_DRV_INT_EN_Msk | CMSDK_WAVE_GEN_DRV_INT_FIRSTADDR_CLR_Msk | CMSDK_WAVE_GEN_DRV_INT_SECONDADDR_CLR_Msk);
}


void TestWaveGeneratorSilentWhenDisabled() {
	CMSDK_WAVE_GEN_TypeDef waveGen = {};
	WaveGeneratorAdapter001 wave(&waveGen);

	waveCalls = 0;
	wave.SetOnInterrupt([](auto) {
		waveCalls++;
	});

	wave.EnableInterrupt(true);
	wave.EnableInterrupt(false);
	ENS_EQ(waveGen.WAVE_GEN_DRV_INT_REG, 0);

	// Both flags are raised while the interrupt is off.
	waveGen.WAVE_GEN_DRV_INT_REG = CMSDK_WAVE_GEN_DRV_INT_FIRSTADDR_STS_Msk | CMSDK_WAVE_GEN_DRV_INT_SECONDADDR_STS_Msk;

	wave.IrqHandler();

	ENS_EQ(waveCalls, 0);

	// The flags are cleared, and the clear does not set the enable bit.
	ENS_EQ(waveGen.WAVE_GEN_DRV_INT_REG, CMSDK_WAVE_GEN_DRV_INT_FIRSTADDR_CLR_Msk | CMSDK_WAVE_GEN_DRV_INT_SECONDADDR_CLR_Msk);
}


void TestWaveGeneratorClearKeepsAddressesAndDisable() {
	// The clear write takes the addresses from the shadow and leaves the enable bit clear, so disabling the interrupt and
	// then clearing a flag keeps the interrupt off.
	CMSDK_WAVE_GEN_TypeDef waveGen = {};
	WaveGeneratorAdapter001 wave(&waveGen);

	waveCalls = 0;
	wave.SetOnInterrupt([](auto) {
		waveCalls++;
	});

	wave.SetInterruptAddresses(5, 9);
	wave.EnableInterrupt(true);
	wave.EnableInterrupt(false);

	waveGen.WAVE_GEN_DRV_INT_REG = CMSDK_WAVE_GEN_DRV_INT_SECONDADDR_STS_Msk;
	wave.IrqHandler();

	ENS_EQ(waveCalls, 0);

	uint32 expected = (5u << CMSDK_WAVE_GEN_DRV_INT_FIRST_ADDR_Pos)
		| (9u << CMSDK_WAVE_GEN_DRV_INT_SECOND_ADDR_Pos)
		| CMSDK_WAVE_GEN_DRV_INT_FIRSTADDR_CLR_Msk
		| CMSDK_WAVE_GEN_DRV_INT_SECONDADDR_CLR_Msk;
	ENS_EQ(waveGen.WAVE_GEN_DRV_INT_REG, expected);
	ENS_EQ(waveGen.WAVE_GEN_DRV_INT_REG & CMSDK_WAVE_GEN_DRV_INT_EN_Msk, 0);
}


}


int main() {
	TestComparatorWithoutTriggerIsSilent();
	TestComparatorRejectsInterruptTriggers();
	TestComparatorNoneKeepsInterruptOff();
	TestWaveGeneratorServesWhenEnabled();
	TestWaveGeneratorSilentWhenDisabled();
	TestWaveGeneratorClearKeepsAddressesAndDisable();

	if (failureCount != 0) {
		std::fprintf(stderr, "TestEnsIrqGates: %d check(s) failed\n", failureCount);
		return 1;
	}

	std::printf("TestEnsIrqGates: all checks passed\n");
	return 0;
}
