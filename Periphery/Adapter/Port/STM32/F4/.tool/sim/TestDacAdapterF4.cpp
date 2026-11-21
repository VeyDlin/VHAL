#include <VHAL.h>
#include <Stm32F4DacSim.h>
#include <Stm32F4DmaSim.h>
#include <Adapter/Port/STM32/F4/DACAdapterF4.h>
#include <cstdio>
#include <vector>


// Host tests for the F4 DAC adapter: the trigger the channel starts with, the enable that follows a configuration, and the
// continuous output by DMA with its checks, stop and underrun report. The register models are in shim/Stm32F4DacSim.h and
// shim/Stm32F4DmaSim.h.


namespace {


int failureCount = 0;

DAC_TypeDef regs;
std::vector<DACAdapterF4::Error> errors;


void Expect(bool condition, const char* expression, int line) {
	if (!condition) {
		failureCount++;
		std::fprintf(stderr, "TestDacAdapterF4.cpp:%d: check failed: %s\n", line, expression);
	}
}


void ExpectEqual(unsigned long long actual, unsigned long long expected, const char* expression, int line) {
	if (actual != expected) {
		failureCount++;
		std::fprintf(stderr, "TestDacAdapterF4.cpp:%d: check failed: %s (actual %llu, expected %llu)\n", line, expression, actual, expected);
	}
}


#define EXPECT(condition) Expect((condition), #condition, __LINE__)
#define EXPECT_EQ(actual, expected) ExpectEqual(static_cast<unsigned long long>(actual), static_cast<unsigned long long>(expected), #actual " == " #expected, __LINE__)


void ResetCase() {
	SimDacResetModel();
	regs = DAC_TypeDef();
	simDma1 = DMA_TypeDef();
	simDma2 = DMA_TypeDef();
	errors.clear();
}


// The address the adapter writes to a DMA register: the low 32 bits of the pointer, as the register takes them.
uint32 AddressOf(const void *address) {
	return static_cast<uint32>(reinterpret_cast<uintptr_t>(address));
}


// A DMA with the DAC request, channel 7, and the given direction and width. The stream is the one the test constructs the
// adapter with.
void ConfigureDma(ADMA &dma, ADMA::Direction direction, ADMA::DataWidth width) {
	ADMA::Parameters params;
	params.peripheral = ADMA::Peripheral::Channel7;
	params.direction = direction;
	params.addressMode = ADMA::AddressMode::IncrementingToFixed;
	params.dataWidth = width;

	EXPECT(dma.SetParameters(params) == ResultStatus::ok);
}


// The DMA the DAC channel is wired to: memory to the holding register, 16-bit items.
void ConfigureDacDma(ADMA &dma) {
	ConfigureDma(dma, ADMA::Direction::MemoryToPeripheral, ADMA::DataWidth::B16);
}


// WriteContinuous() needs a hardware trigger. Each continuous case selects Timer 6 first, so the result depends on the DMA.
void SetTimer6Trigger(DACAdapterF4 &adapter) {
	DACAdapterF4::Parameters params;
	params.triggerSource = DACAdapterF4::TriggerSource::Timer6Trigger;

	adapter.SetParameters(params);
}


void TestDefaultTriggerIsSoftware() {
	ResetCase();
	DACAdapterF4 adapter(&regs, 1);
	DACAdapterF4::Parameters params;

	adapter.SetParameters(params);

	EXPECT_EQ(simDacInitCount, 1);
	EXPECT_EQ(simDacLastTrigger, LL_DAC_TRIG_SOFTWARE);
}



// Timer 6 is the zero code of the register, so a channel that names it must still get it, not software.
void TestNamedTimer6TriggerIsNotSoftware() {
	ResetCase();
	DACAdapterF4 adapter(&regs, 1);
	DACAdapterF4::Parameters params;
	params.triggerSource = DACAdapterF4::TriggerSource::Timer6Trigger;

	adapter.SetParameters(params);

	EXPECT_EQ(simDacLastTrigger, LL_DAC_TRIG_EXT_TIM6_TRGO);
}



void TestNamedTimer2TriggerReachesRegister() {
	ResetCase();
	DACAdapterF4 adapter(&regs, 1);
	DACAdapterF4::Parameters params;
	params.triggerSource = DACAdapterF4::TriggerSource::Timer2Trigger;

	adapter.SetParameters(params);

	EXPECT_EQ(simDacLastTrigger, LL_DAC_TRIG_EXT_TIM2_TRGO);
}



// The channel is enabled only after the initialization succeeded. A refused configuration leaves it off.
void TestEnableSkippedAfterFailedInitialization() {
	ResetCase();
	DACAdapterF4 adapter(&regs, 1);
	adapter.beforePeripheryInit = []() {
		return ResultStatus::error;
	};
	DACAdapterF4::Parameters params;

	adapter.SetParameters(params);

	EXPECT_EQ(simDacInitCount, 0);
	EXPECT_EQ(simDacEnableCount, 0);
}



// No DMA attached: the continuous output and its stop have nothing to drive.
void TestContinuousWithoutDmaIsNotAvailable() {
	ResetCase();
	DACAdapterF4 adapter(&regs, 1);
	uint16 samples[4] = { 1, 2, 3, 4 };

	EXPECT(adapter.WriteContinuous(samples, 4) == ResultStatus::notAvailable);
	EXPECT(adapter.StopDMA() == ResultStatus::notAvailable);
}



// Channel 1 runs on DMA1 Stream 5 with request channel 7. The count is in 16-bit samples and is not doubled.
void TestContinuousStartsDmaOnChannel1() {
	ResetCase();
	DACAdapterF4 adapter(&regs, 1);
	SetTimer6Trigger(adapter);
	ADMA dma(DMA1, 5);
	ConfigureDacDma(dma);
	adapter.SetDMA(&dma);
	uint16 samples[4] = { 0x100, 0x200, 0x300, 0x400 };

	EXPECT(adapter.WriteContinuous(samples, 4) == ResultStatus::ok);

	EXPECT(SimDmaStreamEnabled(DMA1, 5));
	EXPECT_EQ(simDma1.NDTR[5], 4);
	EXPECT_EQ(simDma1.PAR[5], 0x40007408U);
	EXPECT_EQ(simDma1.M0AR[5], AddressOf(samples));
	EXPECT_EQ(simDma1.CR[5] & DMA_SxCR_CHSEL, LL_DMA_CHANNEL_7);
	EXPECT_EQ(simDma1.CR[5] & DMA_SxCR_DIR, LL_DMA_DIRECTION_MEMORY_TO_PERIPH);

	EXPECT((regs.CR & DAC_CR_DMAEN1) != 0);
	EXPECT((regs.CR & DAC_CR_TEN1) != 0);
	EXPECT((regs.CR & DAC_CR_DMAUDRIE1) != 0);
}



// Channel 2 runs on DMA1 Stream 6 with request channel 7, and its own DAC bits.
void TestContinuousStartsDmaOnChannel2() {
	ResetCase();
	DACAdapterF4 adapter(&regs, 2);
	SetTimer6Trigger(adapter);
	ADMA dma(DMA1, 6);
	ConfigureDacDma(dma);
	adapter.SetDMA(&dma);
	uint16 samples[2] = { 0x010, 0x020 };

	EXPECT(adapter.WriteContinuous(samples, 2) == ResultStatus::ok);

	EXPECT(SimDmaStreamEnabled(DMA1, 6));
	EXPECT_EQ(simDma1.NDTR[6], 2);
	EXPECT_EQ(simDma1.PAR[6], 0x40007414U);
	EXPECT_EQ(simDma1.CR[6] & DMA_SxCR_CHSEL, LL_DMA_CHANNEL_7);

	EXPECT((regs.CR & DAC_CR_DMAEN2) != 0);
	EXPECT((regs.CR & DAC_CR_TEN2) != 0);
	EXPECT((regs.CR & DAC_CR_DMAUDRIE2) != 0);
	EXPECT((regs.CR & DAC_CR_DMAEN1) == 0);
}



// Channel 1 on another stream, or on DMA2, is refused: the request is wired to DMA1 Stream 5 only. Nothing is started.
void TestContinuousRefusesWrongStream() {
	ResetCase();
	DACAdapterF4 adapter(&regs, 1);
	SetTimer6Trigger(adapter);
	uint16 samples[4] = { 1, 2, 3, 4 };

	ADMA wrongStream(DMA1, 6);
	ConfigureDacDma(wrongStream);
	adapter.SetDMA(&wrongStream);
	EXPECT(adapter.WriteContinuous(samples, 4) == ResultStatus::invalidParameter);

	ADMA wrongController(DMA2, 5);
	ConfigureDacDma(wrongController);
	adapter.SetDMA(&wrongController);
	EXPECT(adapter.WriteContinuous(samples, 4) == ResultStatus::invalidParameter);

	EXPECT(!SimDmaStreamEnabled(DMA1, 6));
	EXPECT(!SimDmaStreamEnabled(DMA2, 5));
	EXPECT((regs.CR & DAC_CR_DMAEN1) == 0);
}



// Channel 2 on Stream 5 is refused: channel 2 is wired to Stream 6.
void TestContinuousRefusesChannel2OnStream5() {
	ResetCase();
	DACAdapterF4 adapter(&regs, 2);
	SetTimer6Trigger(adapter);
	uint16 samples[4] = { 1, 2, 3, 4 };
	ADMA dma(DMA1, 5);
	ConfigureDacDma(dma);
	adapter.SetDMA(&dma);

	EXPECT(adapter.WriteContinuous(samples, 4) == ResultStatus::invalidParameter);
	EXPECT(!SimDmaStreamEnabled(DMA1, 5));
	EXPECT((regs.CR & DAC_CR_DMAEN2) == 0);
}



// A request other than channel 7 is refused, even on the right stream.
void TestContinuousRefusesWrongRequest() {
	ResetCase();
	DACAdapterF4 adapter(&regs, 1);
	SetTimer6Trigger(adapter);
	uint16 samples[4] = { 1, 2, 3, 4 };
	ADMA dma(DMA1, 5);
	ADMA::Parameters params;
	params.peripheral = ADMA::Peripheral::Channel6;
	params.direction = ADMA::Direction::MemoryToPeripheral;
	params.dataWidth = ADMA::DataWidth::B16;
	EXPECT(dma.SetParameters(params) == ResultStatus::ok);
	adapter.SetDMA(&dma);

	EXPECT(adapter.WriteContinuous(samples, 4) == ResultStatus::invalidParameter);
	EXPECT(!SimDmaStreamEnabled(DMA1, 5));
}



// A byte-width DMA, or a width that differs between the two sides, is refused. Each sample is 16 bits.
void TestContinuousRefusesByteWidth() {
	ResetCase();
	DACAdapterF4 adapter(&regs, 1);
	SetTimer6Trigger(adapter);
	uint16 samples[4] = { 1, 2, 3, 4 };
	ADMA dma(DMA1, 5);
	ConfigureDma(dma, ADMA::Direction::MemoryToPeripheral, ADMA::DataWidth::B8);
	adapter.SetDMA(&dma);

	EXPECT(adapter.WriteContinuous(samples, 4) == ResultStatus::invalidParameter);
	EXPECT(!SimDmaStreamEnabled(DMA1, 5));
}



// The DMA must move from memory to the holding register. The other direction is refused.
void TestContinuousRefusesWrongDirection() {
	ResetCase();
	DACAdapterF4 adapter(&regs, 1);
	SetTimer6Trigger(adapter);
	uint16 samples[4] = { 1, 2, 3, 4 };
	ADMA dma(DMA1, 5);
	ConfigureDma(dma, ADMA::Direction::PeripheralToMemory, ADMA::DataWidth::B16);
	adapter.SetDMA(&dma);

	EXPECT(adapter.WriteContinuous(samples, 4) == ResultStatus::invalidParameter);
	EXPECT(!SimDmaStreamEnabled(DMA1, 5));
}



// A count of zero, or one that does not fit the 16-bit transfer counter, is refused by the DMA and nothing is started.
void TestContinuousRefusesBadCount() {
	ResetCase();
	DACAdapterF4 adapter(&regs, 1);
	SetTimer6Trigger(adapter);
	uint16 samples[4] = { 1, 2, 3, 4 };
	ADMA dma(DMA1, 5);
	ConfigureDacDma(dma);
	adapter.SetDMA(&dma);

	EXPECT(adapter.WriteContinuous(samples, 0) == ResultStatus::invalidParameter);
	EXPECT(adapter.WriteContinuous(samples, 0x10000) == ResultStatus::invalidParameter);
	EXPECT(!SimDmaStreamEnabled(DMA1, 5));
	EXPECT((regs.CR & DAC_CR_DMAEN1) == 0);
}



// A software trigger does not clock continuous output: each DMA request needs a software trigger write. An unset trigger
// is software, and so is a named Software, so both are refused and nothing is started.
void TestContinuousRefusesSoftwareTrigger() {
	ResetCase();
	uint16 samples[4] = { 1, 2, 3, 4 };
	ADMA dma(DMA1, 5);
	ConfigureDacDma(dma);

	DACAdapterF4 unset(&regs, 1);
	unset.SetDMA(&dma);
	EXPECT(unset.WriteContinuous(samples, 4) == ResultStatus::invalidParameter);

	DACAdapterF4 named(&regs, 1);
	DACAdapterF4::Parameters params;
	params.triggerSource = DACAdapterF4::TriggerSource::Software;
	named.SetParameters(params);
	named.SetDMA(&dma);
	EXPECT(named.WriteContinuous(samples, 4) == ResultStatus::invalidParameter);

	EXPECT(!SimDmaStreamEnabled(DMA1, 5));
	EXPECT((regs.CR & DAC_CR_DMAEN1) == 0);
}



// The DMA must increment the memory side and keep the holding register fixed. A peripheral that increments writes successive
// registers, so the output would not repeat the buffer. Such a DMA is refused.
void TestContinuousRefusesPeripheralIncrement() {
	ResetCase();
	DACAdapterF4 adapter(&regs, 1);
	SetTimer6Trigger(adapter);
	uint16 samples[4] = { 1, 2, 3, 4 };
	ADMA dma(DMA1, 5);
	ADMA::Parameters params;
	params.peripheral = ADMA::Peripheral::Channel7;
	params.direction = ADMA::Direction::MemoryToPeripheral;
	params.addressMode = ADMA::AddressMode::FixedToIncrementing;
	params.dataWidth = ADMA::DataWidth::B16;
	EXPECT(dma.SetParameters(params) == ResultStatus::ok);
	adapter.SetDMA(&dma);

	EXPECT(adapter.WriteContinuous(samples, 4) == ResultStatus::invalidParameter);
	EXPECT(!SimDmaStreamEnabled(DMA1, 5));
	EXPECT((regs.CR & DAC_CR_DMAEN1) == 0);
}



// A second start while the stream runs is busy, and the running transfer keeps its settings.
void TestContinuousWhileRunningIsBusy() {
	ResetCase();
	DACAdapterF4 adapter(&regs, 1);
	SetTimer6Trigger(adapter);
	uint16 samples[4] = { 1, 2, 3, 4 };
	uint16 other[4] = { 5, 6, 7, 8 };
	ADMA dma(DMA1, 5);
	ConfigureDacDma(dma);
	adapter.SetDMA(&dma);

	EXPECT(adapter.WriteContinuous(samples, 4) == ResultStatus::ok);
	EXPECT(adapter.WriteContinuous(other, 2) == ResultStatus::busy);
	EXPECT_EQ(simDma1.M0AR[5], AddressOf(samples));
	EXPECT_EQ(simDma1.NDTR[5], 4);
}



// StopDMA stops the stream and clears the request, the trigger and the underrun interrupt of the channel.
void TestStopDmaStopsStreamAndRequest() {
	ResetCase();
	DACAdapterF4 adapter(&regs, 1);
	SetTimer6Trigger(adapter);
	uint16 samples[4] = { 1, 2, 3, 4 };
	ADMA dma(DMA1, 5);
	ConfigureDacDma(dma);
	adapter.SetDMA(&dma);
	EXPECT(adapter.WriteContinuous(samples, 4) == ResultStatus::ok);

	EXPECT(adapter.StopDMA() == ResultStatus::ok);

	EXPECT(!SimDmaStreamEnabled(DMA1, 5));
	EXPECT((regs.CR & DAC_CR_DMAEN1) == 0);
	EXPECT((regs.CR & DAC_CR_TEN1) == 0);
	EXPECT((regs.CR & DAC_CR_DMAUDRIE1) == 0);
}



// An underrun of the channel is reported and its flag cleared. The flag of the other channel is not this channel's.
void TestUnderrunIsReportedPerChannel() {
	ResetCase();
	DACAdapterF4 adapter(&regs, 1);
	adapter.onError = [](DACAdapterF4::Error error) {
		errors.push_back(error);
	};

	regs.SR |= DAC_SR_DMAUDR2;
	adapter.IrqHandler();
	EXPECT_EQ(errors.size(), 0);

	regs.SR |= DAC_SR_DMAUDR1;
	adapter.IrqHandler();
	EXPECT_EQ(errors.size(), 1);
	if (!errors.empty()) {
		EXPECT(errors[0] == DACAdapterF4::Error::DmaUnderrun);
	}
	EXPECT((regs.SR & DAC_SR_DMAUDR1) == 0);
}


} // namespace


int main() {
	TestDefaultTriggerIsSoftware();
	TestNamedTimer6TriggerIsNotSoftware();
	TestNamedTimer2TriggerReachesRegister();
	TestEnableSkippedAfterFailedInitialization();
	TestContinuousWithoutDmaIsNotAvailable();
	TestContinuousStartsDmaOnChannel1();
	TestContinuousStartsDmaOnChannel2();
	TestContinuousRefusesWrongStream();
	TestContinuousRefusesChannel2OnStream5();
	TestContinuousRefusesWrongRequest();
	TestContinuousRefusesByteWidth();
	TestContinuousRefusesWrongDirection();
	TestContinuousRefusesBadCount();
	TestContinuousRefusesSoftwareTrigger();
	TestContinuousRefusesPeripheralIncrement();
	TestContinuousWhileRunningIsBusy();
	TestStopDmaStopsStreamAndRequest();
	TestUnderrunIsReportedPerChannel();

	if (failureCount != 0) {
		std::fprintf(stderr, "%d check(s) failed\n", failureCount);
		return 1;
	}

	std::printf("all DAC checks passed\n");
	return 0;
}
