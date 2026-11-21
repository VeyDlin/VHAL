#include <Adapter/DMAAdapter.h>
#include <cstdio>

// Host tests for the register-free rules of the DMA contract: the count in items of a width, the latched status of a run,
// the check of a request, direction and width against the peripheral, the run mode of a started transfer, and the default
// state of an adapter.


namespace {


int failureCount = 0;


void ExpectEqual(long long actual, long long expected, const char* expression, int line) {
	if (actual != expected) {
		failureCount++;
		std::fprintf(stderr, "TestDMAAdapter.cpp:%d: %s is %lld, expected %lld\n", line, expression, actual, expected);
	}
}


void ExpectTrue(bool condition, const char* expression, int line) {
	if (!condition) {
		failureCount++;
		std::fprintf(stderr, "TestDMAAdapter.cpp:%d: check failed: %s\n", line, expression);
	}
}


#define DMA_EQ(actual, expected) ExpectEqual(static_cast<long long>(actual), static_cast<long long>(expected), #actual, __LINE__)
#define DMA_TRUE(condition) ExpectTrue((condition), #condition, __LINE__)


// A DMA adapter with no registers. The base class leaves the hardware hooks pure, so the test supplies stubs. The stubs follow
// the order of the ports: a start checks the widths, latches the run mode and marks the channel active. A configuration is
// busy while a run is active, as the ports return it.
class FakeDMA : public DMAAdapter<int> {
public:
	FakeDMA() { }

	FakeDMA(int *handle, uint32 channel)
		: DMAAdapter(handle, channel) { }

	virtual void IrqHandler() override { }

	virtual ResultStatus GetStatus() override {
		return ResultStatus::ok;
	}

	virtual ResultStatus Stop() override {
		active = false;
		return ResultStatus::ok;
	}

protected:
	virtual ResultStatus StartTransfer(const uint8*, uint8*, uint32) override {
		ResultStatus status = CheckWidths();
		if (status != ResultStatus::ok) {
			return status;
		}

		LatchRunMode();
		active = true;
		return ResultStatus::ok;
	}

	virtual ResultStatus Initialization() override {
		return active ? ResultStatus::busy : ResultStatus::ok;
	}

private:
	bool active = false;
};


using Dma = FakeDMA;
using Width = Dma::DataWidth;
using Dir = Dma::Direction;


Dma MakeDma(Dir direction, uint32 request, Width memory, Width periph) {
	Dma dma;
	Dma::Parameters params;
	params.peripheral = Dma::DMAPeripheralOption(request);
	params.direction = direction;
	params.addressMode = Dma::AddressMode::IncrementingToFixed;
	params.dataWidth = memory;
	params.periphDataWidth = periph;
	dma.SetParameters(params);
	return dma;
}


void TestItemCount() {
	uint32 count = 1234;

	DMA_EQ(Dma::ItemCount(0, 1, 0xFFFF, count), static_cast<int>(ResultStatus::invalidParameter));
	DMA_EQ(count, 0);

	DMA_EQ(Dma::ItemCount(5, 2, 0xFFFF, count), static_cast<int>(ResultStatus::invalidParameter));
	DMA_EQ(Dma::ItemCount(4, 2, 0xFFFF, count), static_cast<int>(ResultStatus::ok));
	DMA_EQ(count, 2);

	DMA_EQ(Dma::ItemCount(7, 1, 0xFFFF, count), static_cast<int>(ResultStatus::ok));
	DMA_EQ(count, 7);

	DMA_EQ(Dma::ItemCount(0x10000, 1, 0xFFFF, count), static_cast<int>(ResultStatus::invalidParameter));
	DMA_EQ(Dma::ItemCount(0xFFFF, 1, 0xFFFF, count), static_cast<int>(ResultStatus::ok));
	DMA_EQ(count, 0xFFFF);

	DMA_EQ(Dma::ItemCount(0x1FFFE, 2, 0xFFFF, count), static_cast<int>(ResultStatus::ok));
	DMA_EQ(count, 0xFFFF);

	DMA_EQ(Dma::ItemCount(0x20000, 2, 0xFFFF, count), static_cast<int>(ResultStatus::invalidParameter));

	DMA_EQ(Dma::ItemCount(8, 0, 0xFFFF, count), static_cast<int>(ResultStatus::invalidParameter));
}


// A 16-bit caller count doubled into bytes must come out as the same number of halfwords, never twice that.
void TestHalfwordCountIsNotDoubled() {
	uint32 count = 0;
	uint32 samples = 100;

	DMA_EQ(Dma::ItemCount(samples * sizeof(uint16), 2, 0xFFFF, count), static_cast<int>(ResultStatus::ok));
	DMA_EQ(count, samples);

	// The same bytes on a byte-width DMA would be twice the transfers. The DAC refuses that DMA instead (see TestCheckPeripheral).
	DMA_EQ(Dma::ItemCount(samples * sizeof(uint16), 1, 0xFFFF, count), static_cast<int>(ResultStatus::ok));
	DMA_EQ(count, 2 * samples);
}


// ADC at 6 and 8 bits: one byte per sample. At 10 and 12 bits: one halfword per sample.
void TestAdcSampleCount() {
	uint32 count = 0;
	uint32 samples = 64;

	DMA_EQ(Dma::ItemCount(samples * 1, 1, 0xFFFF, count), static_cast<int>(ResultStatus::ok));
	DMA_EQ(count, samples);

	DMA_EQ(Dma::ItemCount(samples * 2, 2, 0xFFFF, count), static_cast<int>(ResultStatus::ok));
	DMA_EQ(count, samples);
}


void TestLatchedStatus() {
	// Error wins over everything.
	DMA_EQ(Dma::LatchedStatus(true, true, false, true), static_cast<int>(ResultStatus::error));
	DMA_EQ(Dma::LatchedStatus(true, false, true, false), static_cast<int>(ResultStatus::error));

	// Normal mode: completion latched is ok, running and not complete is busy, stopped and not complete is stop.
	DMA_EQ(Dma::LatchedStatus(false, true, false, true), static_cast<int>(ResultStatus::ok));
	DMA_EQ(Dma::LatchedStatus(false, true, false, false), static_cast<int>(ResultStatus::ok));
	DMA_EQ(Dma::LatchedStatus(false, false, false, true), static_cast<int>(ResultStatus::busy));
	DMA_EQ(Dma::LatchedStatus(false, false, false, false), static_cast<int>(ResultStatus::stop));

	// Circular mode never completes: ok while the channel runs, stop once it is off.
	DMA_EQ(Dma::LatchedStatus(false, false, true, true), static_cast<int>(ResultStatus::ok));
	DMA_EQ(Dma::LatchedStatus(false, false, true, false), static_cast<int>(ResultStatus::stop));
	DMA_EQ(Dma::LatchedStatus(false, true, true, false), static_cast<int>(ResultStatus::stop));
}


void TestCheckPeripheral() {
	const uint32 request = 7;

	Dma matching = MakeDma(Dir::PeripheralToMemory, request, Width::B16, static_cast<Width>(0));
	DMA_EQ(matching.CheckPeripheral(request, Dir::PeripheralToMemory, Width::B16), static_cast<int>(ResultStatus::ok));

	DMA_EQ(matching.CheckPeripheral(request + 1, Dir::PeripheralToMemory, Width::B16), static_cast<int>(ResultStatus::invalidParameter));
	DMA_EQ(matching.CheckPeripheral(request, Dir::MemoryToPeripheral, Width::B16), static_cast<int>(ResultStatus::invalidParameter));
	DMA_EQ(matching.CheckPeripheral(request, Dir::PeripheralToMemory, Width::B8), static_cast<int>(ResultStatus::invalidParameter));
	DMA_EQ(matching.CheckPeripheral(request, Dir::PeripheralToMemory, Width::B32), static_cast<int>(ResultStatus::invalidParameter));

	// A byte-width DMA is refused for a halfword peripheral: the DAC case.
	Dma byteWide = MakeDma(Dir::MemoryToPeripheral, request, Width::B8, static_cast<Width>(0));
	DMA_EQ(byteWide.CheckPeripheral(request, Dir::MemoryToPeripheral, Width::B16), static_cast<int>(ResultStatus::invalidParameter));

	// A peripheral width that differs from the memory width is refused, even when the memory width matches.
	Dma mixed = MakeDma(Dir::PeripheralToMemory, request, Width::B16, Width::B8);
	DMA_EQ(mixed.CheckPeripheral(request, Dir::PeripheralToMemory, Width::B16), static_cast<int>(ResultStatus::invalidParameter));
	DMA_EQ(mixed.CheckPeripheral(request, Dir::PeripheralToMemory, Width::B8), static_cast<int>(ResultStatus::invalidParameter));

	Dma equalSides = MakeDma(Dir::PeripheralToMemory, request, Width::B16, Width::B16);
	DMA_EQ(equalSides.CheckPeripheral(request, Dir::PeripheralToMemory, Width::B16), static_cast<int>(ResultStatus::ok));
}


void TestPeriphWidth() {
	Dma inherits = MakeDma(Dir::MemoryToPeripheral, 1, Width::B32, static_cast<Width>(0));
	DMA_EQ(inherits.GetPeriphDataWidth(), static_cast<int>(Width::B32));

	Dma explicitWidth = MakeDma(Dir::MemoryToPeripheral, 1, Width::B32, Width::B16);
	DMA_EQ(explicitWidth.GetPeriphDataWidth(), static_cast<int>(Width::B16));
}


// The transfer counter is defined only when both sides have the same width. A pair that differs is refused at the start, so
// a count is never read as a number of items of two different widths.
void TestWidthsMatch() {
	Dma inherits = MakeDma(Dir::PeripheralToMemory, 1, Width::B32, static_cast<Width>(0));
	DMA_EQ(inherits.CheckWidths(), static_cast<int>(ResultStatus::ok));

	Dma equal = MakeDma(Dir::MemoryToPeripheral, 1, Width::B16, Width::B16);
	DMA_EQ(equal.CheckWidths(), static_cast<int>(ResultStatus::ok));

	Dma mismatchedToPeripheral = MakeDma(Dir::MemoryToPeripheral, 1, Width::B16, Width::B8);
	DMA_EQ(mismatchedToPeripheral.CheckWidths(), static_cast<int>(ResultStatus::invalidParameter));

	Dma mismatchedToMemory = MakeDma(Dir::PeripheralToMemory, 1, Width::B16, Width::B8);
	DMA_EQ(mismatchedToMemory.CheckWidths(), static_cast<int>(ResultStatus::invalidParameter));

	Dma mismatchedMemoryToMemory = MakeDma(Dir::MemoryToMemory, 1, Width::B8, Width::B32);
	DMA_EQ(mismatchedMemoryToMemory.CheckWidths(), static_cast<int>(ResultStatus::invalidParameter));
}


// A start with mismatched widths is refused before it touches the channel, so no run is latched.
void TestMismatchedWidthIsNotStarted() {
	Dma dma = MakeDma(Dir::PeripheralToMemory, 7, Width::B16, Width::B8);

	uint16 source[4] = { 0, 0, 0, 0 };
	uint16 target[4] = { 0, 0, 0, 0 };
	DMA_EQ(dma.Start(source, target, 4), static_cast<int>(ResultStatus::invalidParameter));
	DMA_TRUE(dma.GetParameters().periphDataWidth == Width::B8);
}


// The run mode is latched at the start. A mode change while the channel runs is refused and does not change the run; the
// new mode applies to the next run.
void TestRunModeIsLatchedAtStart() {
	Dma dma = MakeDma(Dir::PeripheralToMemory, 7, Width::B16, Width::B16);

	uint16 source[4] = { 0, 0, 0, 0 };
	uint16 target[4] = { 0, 0, 0, 0 };
	DMA_EQ(dma.Start(source, target, 4), static_cast<int>(ResultStatus::ok));
	DMA_TRUE(dma.IsRunCircular() == false);

	Dma::Parameters changed = dma.GetParameters();
	changed.circularMode = true;
	DMA_EQ(dma.SetParameters(changed), static_cast<int>(ResultStatus::busy));
	DMA_TRUE(dma.IsRunCircular() == false);
	DMA_TRUE(dma.GetParameters().circularMode == true);

	dma.Stop();
	DMA_EQ(dma.Start(source, target, 4), static_cast<int>(ResultStatus::ok));
	DMA_TRUE(dma.IsRunCircular() == true);
}


void TestDefaultState() {
	Dma dma;
	DMA_EQ(dma.GetLastTransferSize(), 0);
	DMA_TRUE(dma.GetParameters().circularMode == false);
	DMA_TRUE(dma.GetParameters().direction == Dir::MemoryToPeripheral);
	DMA_TRUE(dma.GetParameters().addressMode == Dma::AddressMode::IncrementingToFixed);
	DMA_EQ(dma.GetParameters().dataWidth, static_cast<int>(Width::B8));
	DMA_TRUE(dma.onHalfTransfer == nullptr);
	DMA_TRUE(dma.IsRunCircular() == false);
}


}  // namespace



int main() {
	TestItemCount();
	TestHalfwordCountIsNotDoubled();
	TestAdcSampleCount();
	TestLatchedStatus();
	TestCheckPeripheral();
	TestPeriphWidth();
	TestWidthsMatch();
	TestMismatchedWidthIsNotStarted();
	TestRunModeIsLatchedAtStart();
	TestDefaultState();

	if (failureCount != 0) {
		std::fprintf(stderr, "TestDMAAdapter: %d check(s) failed\n", failureCount);
		return 1;
	}

	std::printf("TestDMAAdapter: all checks passed\n");
	return 0;
}
