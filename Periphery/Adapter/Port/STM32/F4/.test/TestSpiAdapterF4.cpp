#include <VHAL.h>
#include <Stm32F4SpiSim.h>
#include <Adapter/Port/STM32/F4/SPIAdapterF4.h>
#include <cstdio>
#include <vector>


// Host tests for the F4 SPI adapter: the overrun report of a write, the clock of a blocking master read, the size limit of
// the 16-bit transfer counters, and continuous receive stopping without touching a write. The register model is in
// shim/Stm32F4SpiSim.h. Each interrupt is driven by hand, so every step is fixed.


namespace {


int failureCount = 0;

SPI_TypeDef regs;
std::vector<SPIAdapterF4::Error> errors;
int txInterruptCount = 0;


void Expect(bool condition, const char* expression, int line) {
	if (!condition) {
		failureCount++;
		std::fprintf(stderr, "TestSpiAdapterF4.cpp:%d: check failed: %s\n", line, expression);
	}
}


void ExpectEqual(unsigned long long actual, unsigned long long expected, const char* expression, int line) {
	if (actual != expected) {
		failureCount++;
		std::fprintf(stderr, "TestSpiAdapterF4.cpp:%d: check failed: %s (actual %llu, expected %llu)\n", line, expression, actual, expected);
	}
}


#define EXPECT(condition) Expect((condition), #condition, __LINE__)
#define EXPECT_EQ(actual, expected) ExpectEqual(static_cast<unsigned long long>(actual), static_cast<unsigned long long>(expected), #actual " == " #expected, __LINE__)


void ResetCase() {
	SimSpiResetModel();
	regs = SPI_TypeDef();
	errors.clear();
	txInterruptCount = 0;
}


void WireCallbacks(SPIAdapterF4 &adapter) {
	adapter.onError = [](SPIAdapterF4::Error error) {
		errors.push_back(error);
	};
	adapter.onInterrupt = [](SPIAdapterF4::Irq irq) {
		if (irq == SPIAdapterF4::Irq::Tx) {
			txInterruptCount++;
		}
	};
}


// A write only sends. The receive register fills with each byte and nobody reads it, so the overrun is expected on the
// device. The write is not a fault, and it must not be reported.
void TestWriteOnlyDoesNotReportOverrun() {
	ResetCase();
	SPIAdapterF4 adapter(&regs);
	WireCallbacks(adapter);
	uint8 data[3] = { 0x41, 0x42, 0x43 };

	EXPECT(adapter.WriteArrayAsync<uint8>(data, 3) == ResultStatus::ok);
	adapter.IrqHandler();
	adapter.IrqHandler();
	adapter.IrqHandler();

	EXPECT(simSpiWritten == std::vector<uint8>({ 0x41, 0x42, 0x43 }));
	EXPECT_EQ(errors.size(), 0);
	EXPECT_EQ(txInterruptCount, 1);
}



// An overrun during a read is still a fault: the byte that was read is lost.
void TestOverrunDuringReadIsReported() {
	ResetCase();
	SPIAdapterF4 adapter(&regs);
	WireCallbacks(adapter);
	uint8 buffer[1] = { 0 };

	EXPECT(adapter.ReadArrayAsync<uint8>(buffer, 1) == ResultStatus::ok);
	regs.SR |= SPI_SR_OVR;
	adapter.IrqHandler();

	EXPECT_EQ(errors.size(), 1);
	if (!errors.empty()) {
		EXPECT(errors[0] == SPIAdapterF4::Error::Overrun);
	}
}



// The transfer counters are 16 bits. A longer transfer is refused, and it is not cut to its low 16 bits.
void TestSizesAboveSixteenBitsAreRefused() {
	ResetCase();
	SPIAdapterF4 adapter(&regs);
	std::vector<uint8> big(65536, 0);

	EXPECT(adapter.ReadArrayAsync<uint8>(big.data(), 65536) == ResultStatus::invalidParameter);
	EXPECT(adapter.WriteArrayAsync<uint8>(big.data(), 65536) == ResultStatus::invalidParameter);
	EXPECT(adapter.WriteReadArrayAsync<uint8>(big.data(), big.data(), 65536) == ResultStatus::invalidParameter);
	EXPECT(adapter.ReadArray<uint8>(big.data(), 65536) == ResultStatus::invalidParameter);
	EXPECT(adapter.WriteArray<uint8>(big.data(), 65536) == ResultStatus::invalidParameter);
	EXPECT(adapter.WriteReadArray<uint8>(big.data(), big.data(), 65536) == ResultStatus::invalidParameter);
	EXPECT_EQ(simSpiWritten.size(), 0);
}



// A master read has to run the clock: each byte is clocked out as 0xFF, and the received byte is taken after it.
void TestBlockingMasterReadRunsClock() {
	ResetCase();
	SPIAdapterF4 adapter(&regs);
	simSpiRxQueue = { 0x41, 0x42 };
	uint8 buffer[2] = { 0, 0 };

	EXPECT(adapter.ReadArray<uint8>(buffer, 2) == ResultStatus::ok);
	EXPECT(simSpiWritten == std::vector<uint8>({ 0xFF, 0xFF }));
	EXPECT_EQ(buffer[0], 0x41);
	EXPECT_EQ(buffer[1], 0x42);
}



// A single Read() is a master read of one byte: the byte is clocked out as 0xFF, and the byte that comes back is returned.
void TestSingleReadClocksOneByte() {
	ResetCase();
	SPIAdapterF4 adapter(&regs);
	simSpiRxQueue = { 0x5A };

	Result<uint8> result = adapter.Read<uint8>();

	EXPECT(result.IsOk());
	EXPECT(simSpiWritten == std::vector<uint8>({ 0xFF }));
	if (result.IsOk()) {
		EXPECT_EQ(result.Value(), 0x5A);
	}
}



// An async read clocks 0xFF for each byte and fills the buffer in order. Both interrupts are off when the read has ended.
void TestAsyncReadClocksAndFillsBuffer() {
	ResetCase();
	SPIAdapterF4 adapter(&regs);
	WireCallbacks(adapter);
	simSpiRxQueue = { 0x41, 0x42 };
	uint8 buffer[2] = { 0, 0 };

	EXPECT(adapter.ReadArrayAsync<uint8>(buffer, 2) == ResultStatus::ok);

	// Each handler call serves the errors, the received byte and the next clock: three calls finish two bytes.
	for (uint32 step = 0; step < 3; step++) {
		adapter.IrqHandler();
	}

	EXPECT(simSpiWritten == std::vector<uint8>({ 0xFF, 0xFF }));
	EXPECT_EQ(buffer[0], 0x41);
	EXPECT_EQ(buffer[1], 0x42);
	EXPECT((regs.CR2 & (SPI_CR2_RXNEIE | SPI_CR2_TXEIE)) == 0);
	EXPECT_EQ(txInterruptCount, 0);
}



// Stopping continuous receive stops the receive side only. A write that is running keeps going.
void TestStopContinuousReceiveKeepsWriteRunning() {
	ResetCase();
	SPIAdapterF4 adapter(&regs);
	WireCallbacks(adapter);
	uint8 data[2] = { 0x51, 0x52 };

	EXPECT(adapter.SetContinuousAsyncRxMode(true) == ResultStatus::ok);
	EXPECT(adapter.WriteArrayAsync<uint8>(data, 2) == ResultStatus::ok);
	EXPECT(adapter.SetContinuousAsyncRxMode(false) == ResultStatus::ok);

	adapter.IrqHandler();
	adapter.IrqHandler();

	EXPECT(simSpiWritten == std::vector<uint8>({ 0x51, 0x52 }));
	EXPECT_EQ(txInterruptCount, 1);
	EXPECT_EQ(errors.size(), 0);
}


} // namespace


int main() {
	TestWriteOnlyDoesNotReportOverrun();
	TestOverrunDuringReadIsReported();
	TestSizesAboveSixteenBitsAreRefused();
	TestBlockingMasterReadRunsClock();
	TestSingleReadClocksOneByte();
	TestAsyncReadClocksAndFillsBuffer();
	TestStopContinuousReceiveKeepsWriteRunning();

	if (failureCount != 0) {
		std::fprintf(stderr, "%d check(s) failed\n", failureCount);
		return 1;
	}

	std::printf("all SPI checks passed\n");
	return 0;
}
