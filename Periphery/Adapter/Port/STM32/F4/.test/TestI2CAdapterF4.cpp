#include <VHAL.h>
#include <Stm32F4I2cSim.h>
#include <Adapter/Port/STM32/F4/I2CAdapterF4.h>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>


// Host tests for the F4 I2C master: the register sequence of the asynchronous read and write, the completion that ends the
// transfer, the error path, the IrqHandler dispatch, the scan range and the retry count of CheckDevice. The register model
// is in shim/Stm32F4I2cSim.h. The test sets the status flags before each interrupt, so every step of the sequence is fixed.
// The checks never wait on a flag that the model does not raise, so a failing build reports a failure and does not hang.


namespace {


int failureCount = 0;

I2C_TypeDef regs;
std::vector<I2CAdapterF4::Error> errors;
int completeCount = 0;


void Expect(bool condition, const char* expression, int line) {
	if (!condition) {
		failureCount++;
		std::fprintf(stderr, "TestI2CAdapterF4.cpp:%d: check failed: %s\n", line, expression);
	}
}


void ExpectEqual(unsigned long long actual, unsigned long long expected, const char* expression, int line) {
	if (actual != expected) {
		failureCount++;
		std::fprintf(stderr, "TestI2CAdapterF4.cpp:%d: check failed: %s (actual %llu, expected %llu)\n", line, expression, actual, expected);
	}
}


#define EXPECT(condition) Expect((condition), #condition, __LINE__)
#define EXPECT_EQ(actual, expected) ExpectEqual(static_cast<unsigned long long>(actual), static_cast<unsigned long long>(expected), #actual " == " #expected, __LINE__)


void ResetCase() {
	SimResetModel();
	regs.CR1 = 0;
	regs.CR2 = 0;
	regs.SR1 = 0;
	regs.SR2 = 0;
	errors.clear();
	completeCount = 0;
}


void SetFlags(uint32 sr1) {
	regs.SR1 = sr1;
}


uint32 LastWritten() {
	return simWritten.empty() ? 0xFFFFFFFF : simWritten.back();
}


bool AckEnabled() {
	return (regs.CR1 & I2C_CR1_ACK) != 0;
}


bool ItBufEnabled() {
	return (regs.CR2 & I2C_CR2_ITBUFEN) != 0;
}


void WireCallbacks(I2CAdapterF4 &adapter) {
	adapter.onComplete = []() {
		completeCount++;
	};
	adapter.onError = [](I2CAdapterF4::Error error) {
		errors.push_back(error);
	};
}


// Runs the start of a read with a one-byte register address: START, device address for write, the register byte, a
// repeated START, and the device address for read. The caller continues from the address-for-read interrupt.
void ReadPreamble(I2CAdapterF4 &adapter, uint8 out[], uint32 size) {
	EXPECT(adapter.ReadByteArrayAsync(0x50, 0x10, 1, out, size) == ResultStatus::ok);

	SetFlags(I2C_SR1_SB);
	adapter.IrqEventHandler();
	EXPECT_EQ(LastWritten(), 0xA0);

	SetFlags(I2C_SR1_ADDR);
	adapter.IrqEventHandler();

	SetFlags(I2C_SR1_TXE);
	adapter.IrqEventHandler();
	EXPECT_EQ(LastWritten(), 0x10);

	SetFlags(I2C_SR1_BTF);
	adapter.IrqEventHandler();
	EXPECT_EQ(simStartCount, 2);

	SetFlags(I2C_SR1_SB);
	adapter.IrqEventHandler();
	EXPECT_EQ(LastWritten(), 0xA1);
}


void TestClockSpeedIsInHertz() {
	ResetCase();
	I2CAdapterF4 adapter(&regs, 42000000);

	I2CAdapterF4::Parameters params;
	params.speed = I2CAdapterF4::Speed::Standard;
	adapter.SetParameters(params);
	EXPECT_EQ(simInitClockSpeed, 100000);

	params.speed = I2CAdapterF4::Speed::Fast;
	adapter.SetParameters(params);
	EXPECT_EQ(simInitClockSpeed, 400000);
}


void TestAsyncReadWithRegisterThreeBytes() {
	ResetCase();
	I2CAdapterF4 adapter(&regs, 42000000);
	WireCallbacks(adapter);
	simRxQueue = { 0x11, 0x22, 0x33 };
	uint8 out[3] = { 0, 0, 0 };

	ReadPreamble(adapter, out, 3);
	EXPECT(regs.CR1 & I2C_CR1_PE);
	EXPECT(adapter.ReadByteArrayAsync(0x50, 0x10, 1, out, 3) == ResultStatus::busy);

	SetFlags(I2C_SR1_ADDR);
	adapter.IrqEventHandler();
	EXPECT(AckEnabled());
	EXPECT_EQ(simWritten.size(), 3);

	SetFlags(I2C_SR1_BTF);
	adapter.IrqEventHandler();
	EXPECT(!AckEnabled());
	EXPECT_EQ(out[0], 0x11);

	SetFlags(I2C_SR1_BTF);
	adapter.IrqEventHandler();
	EXPECT_EQ(simStopCount, 1);
	EXPECT_EQ(out[1], 0x22);
	EXPECT_EQ(out[2], 0x33);
	EXPECT_EQ(completeCount, 1);

	if (completeCount == 1) {
		EXPECT(adapter.Await() == ResultStatus::ok);
	}
	EXPECT(adapter.ReadByteArrayAsync(0x50, 0x10, 1, out, 3) == ResultStatus::ok);
}


void TestAsyncReadOneByte() {
	ResetCase();
	I2CAdapterF4 adapter(&regs, 42000000);
	WireCallbacks(adapter);
	simRxQueue = { 0x5A };
	uint8 out[1] = { 0 };

	ReadPreamble(adapter, out, 1);

	SetFlags(I2C_SR1_ADDR);
	adapter.IrqEventHandler();
	EXPECT(!AckEnabled());
	EXPECT_EQ(simStopCount, 1);
	EXPECT(ItBufEnabled());

	SetFlags(I2C_SR1_RXNE);
	adapter.IrqEventHandler();
	EXPECT_EQ(out[0], 0x5A);
	EXPECT_EQ(completeCount, 1);
}


void TestAsyncReadTwoBytes() {
	ResetCase();
	I2CAdapterF4 adapter(&regs, 42000000);
	WireCallbacks(adapter);
	simRxQueue = { 0x0A, 0x0B };
	uint8 out[2] = { 0, 0 };

	ReadPreamble(adapter, out, 2);

	SetFlags(I2C_SR1_ADDR);
	adapter.IrqEventHandler();
	EXPECT(!AckEnabled());
	EXPECT(regs.CR1 & I2C_CR1_POS);
	EXPECT(!ItBufEnabled());

	SetFlags(I2C_SR1_BTF);
	adapter.IrqEventHandler();
	EXPECT_EQ(simStopCount, 1);
	EXPECT_EQ(out[0], 0x0A);
	EXPECT_EQ(out[1], 0x0B);
	EXPECT_EQ(completeCount, 1);
}


void TestAsyncReadFiveBytes() {
	ResetCase();
	I2CAdapterF4 adapter(&regs, 42000000);
	WireCallbacks(adapter);
	simRxQueue = { 1, 2, 3, 4, 5 };
	uint8 out[5] = { 0, 0, 0, 0, 0 };

	ReadPreamble(adapter, out, 5);

	SetFlags(I2C_SR1_ADDR);
	adapter.IrqEventHandler();
	EXPECT(ItBufEnabled());

	SetFlags(I2C_SR1_RXNE);
	adapter.IrqEventHandler();
	EXPECT_EQ(out[0], 1);

	SetFlags(I2C_SR1_RXNE);
	adapter.IrqEventHandler();
	EXPECT_EQ(out[1], 2);
	EXPECT(!ItBufEnabled());

	SetFlags(I2C_SR1_BTF);
	adapter.IrqEventHandler();
	EXPECT(!AckEnabled());
	EXPECT_EQ(out[2], 3);

	SetFlags(I2C_SR1_BTF);
	adapter.IrqEventHandler();
	EXPECT_EQ(simStopCount, 1);
	EXPECT_EQ(out[3], 4);
	EXPECT_EQ(out[4], 5);
	EXPECT_EQ(completeCount, 1);
}


void TestAsyncWriteRegisterThenData() {
	ResetCase();
	I2CAdapterF4 adapter(&regs, 42000000);
	WireCallbacks(adapter);
	uint8 data[2] = { 0xD0, 0xD1 };

	EXPECT(adapter.WriteByteArrayAsync(0x50, 0x0102, 2, data, 2) == ResultStatus::ok);
	EXPECT(regs.CR1 & I2C_CR1_PE);

	SetFlags(I2C_SR1_SB);
	adapter.IrqEventHandler();
	EXPECT_EQ(LastWritten(), 0xA0);

	SetFlags(I2C_SR1_ADDR);
	adapter.IrqEventHandler();
	EXPECT_EQ(simWritten.size(), 1);

	SetFlags(I2C_SR1_TXE);
	adapter.IrqEventHandler();
	EXPECT_EQ(LastWritten(), 0x01);

	SetFlags(I2C_SR1_TXE);
	adapter.IrqEventHandler();
	EXPECT_EQ(LastWritten(), 0x02);

	SetFlags(I2C_SR1_TXE);
	adapter.IrqEventHandler();
	EXPECT_EQ(LastWritten(), 0xD0);

	SetFlags(I2C_SR1_TXE);
	adapter.IrqEventHandler();
	EXPECT_EQ(LastWritten(), 0xD1);
	EXPECT(!ItBufEnabled());

	SetFlags(I2C_SR1_BTF);
	adapter.IrqEventHandler();
	EXPECT_EQ(simStopCount, 1);
	EXPECT_EQ(completeCount, 1);
	EXPECT_EQ(simWritten.size(), 5);

	if (completeCount == 1) {
		EXPECT(adapter.Await() == ResultStatus::ok);
	}
	EXPECT(adapter.WriteByteArrayAsync(0x50, 0x10, 1, data, 1) == ResultStatus::ok);
}


void TestAsyncWriteRegisterOnly() {
	ResetCase();
	I2CAdapterF4 adapter(&regs, 42000000);
	WireCallbacks(adapter);
	uint8 unused[1] = { 0 };

	EXPECT(adapter.WriteByteArrayAsync(0x50, 0x33, 1, unused, 0) == ResultStatus::ok);

	SetFlags(I2C_SR1_SB);
	adapter.IrqEventHandler();
	SetFlags(I2C_SR1_ADDR);
	adapter.IrqEventHandler();
	SetFlags(I2C_SR1_TXE);
	adapter.IrqEventHandler();
	EXPECT_EQ(LastWritten(), 0x33);
	EXPECT(!ItBufEnabled());

	SetFlags(I2C_SR1_BTF);
	adapter.IrqEventHandler();
	EXPECT_EQ(simStopCount, 1);
	EXPECT_EQ(completeCount, 1);
}


void TestAcknowledgeFailureEndsTransfer() {
	ResetCase();
	I2CAdapterF4 adapter(&regs, 42000000);
	WireCallbacks(adapter);
	uint8 data[1] = { 0x01 };

	EXPECT(adapter.WriteByteArrayAsync(0x50, 0x10, 1, data, 1) == ResultStatus::ok);
	SetFlags(I2C_SR1_SB);
	adapter.IrqEventHandler();

	SetFlags(I2C_SR1_AF);
	adapter.IrqErrorHandler();

	EXPECT_EQ(errors.size(), 1);
	if (!errors.empty()) {
		EXPECT(errors[0] == I2CAdapterF4::Error::AcknowledgeFailure);
		EXPECT(!(regs.SR1 & I2C_SR1_AF));
		EXPECT_EQ(simStopCount, 1);
		EXPECT(!(regs.CR2 & I2C_CR2_ITEVTEN));
		EXPECT(adapter.Await() == ResultStatus::error);
	}
	EXPECT(adapter.WriteByteArrayAsync(0x50, 0x10, 1, data, 1) == ResultStatus::ok);
}


void TestErrorFlagsAreReported() {
	struct Case {
		uint32 flag;
		I2CAdapterF4::Error expected;
	};
	const Case cases[] = {
		{ I2C_SR1_BERR, I2CAdapterF4::Error::MisplacedStartStop },
		{ I2C_SR1_ARLO, I2CAdapterF4::Error::ArbitrationLost },
		{ I2C_SR1_OVR, I2CAdapterF4::Error::OverUnderRun },
	};

	for (const Case &item : cases) {
		ResetCase();
		I2CAdapterF4 adapter(&regs, 42000000);
		WireCallbacks(adapter);
		uint8 data[1] = { 0x01 };

		EXPECT(adapter.ReadByteArrayAsync(0x50, 0x10, 1, data, 1) == ResultStatus::ok);
		SetFlags(item.flag);
		adapter.IrqErrorHandler();

		EXPECT_EQ(errors.size(), 1);
		if (!errors.empty()) {
			EXPECT(errors[0] == item.expected);
		}
		EXPECT(!(regs.SR1 & item.flag));
		EXPECT(!(regs.CR2 & I2C_CR2_ITERREN));
	}
}


void TestIrqHandlerDispatchesEventsAndErrors() {
	ResetCase();
	I2CAdapterF4 adapter(&regs, 42000000);
	WireCallbacks(adapter);
	uint8 data[1] = { 0x01 };

	EXPECT(adapter.WriteByteArrayAsync(0x50, 0x10, 1, data, 1) == ResultStatus::ok);

	SetFlags(I2C_SR1_SB);
	adapter.IrqHandler();
	EXPECT_EQ(LastWritten(), 0xA0);
	EXPECT_EQ(errors.size(), 0);

	SetFlags(I2C_SR1_AF);
	adapter.IrqHandler();
	EXPECT_EQ(errors.size(), 1);
	if (!errors.empty()) {
		EXPECT(errors[0] == I2CAdapterF4::Error::AcknowledgeFailure);
	}
}


void TestReadWithZeroSizeIsRejected() {
	ResetCase();
	I2CAdapterF4 adapter(&regs, 42000000);
	uint8 out[1] = { 0 };

	EXPECT(adapter.ReadByteArrayAsync(0x50, 0x10, 1, out, 0) == ResultStatus::invalidParameter);
	EXPECT_EQ(simStartCount, 0);
	EXPECT(adapter.WriteByteArrayAsync(0x50, 0x10, 1, out, 1) == ResultStatus::ok);
}


void TestScanFindsOnlyUserAddresses() {
	ResetCase();
	I2CAdapterF4 adapter(&regs, 42000000);
	simAutoAck = true;
	simAckDevices = { 0x03, 0x3C, 0x50, 0x78 };
	uint8 list[8] = { 0 };

	Result<uint8> result = adapter.Scan(list, 8);
	EXPECT(result.IsOk());
	EXPECT_EQ(result.Value(), 2);
	EXPECT_EQ(list[0], 0x3C);
	EXPECT_EQ(list[1], 0x50);
}


void TestScanStopsWhenListIsFull() {
	ResetCase();
	I2CAdapterF4 adapter(&regs, 42000000);
	simAutoAck = true;
	simAckDevices = { 0x10, 0x11, 0x12 };
	uint8 list[2] = { 0 };

	Result<uint8> result = adapter.Scan(list, 2);
	EXPECT(result.IsOk());
	EXPECT_EQ(result.Value(), 2);
	EXPECT_EQ(list[0], 0x10);
	EXPECT_EQ(list[1], 0x11);
}


void TestCheckDeviceRetriesRepeatCount() {
	ResetCase();
	I2CAdapterF4 adapter(&regs, 42000000);
	simAutoAck = true;

	EXPECT(adapter.CheckDevice(0x55, 3) == ResultStatus::error);
	EXPECT_EQ(simStartCount, 3);
}



// The interrupt handlers run on another thread, as they do on the device. Await must last until the transfer ends.
void TestAwaitWaitsForWorkerThread() {
	ResetCase();
	I2CAdapterF4 adapter(&regs, 42000000);
	WireCallbacks(adapter);
	uint8 data[1] = { 0x01 };

	EXPECT(adapter.WriteByteArrayAsync(0x50, 0x10, 1, data, 1) == ResultStatus::ok);

	std::thread worker([&adapter]() {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		SetFlags(I2C_SR1_SB);
		adapter.IrqEventHandler();
		SetFlags(I2C_SR1_ADDR);
		adapter.IrqEventHandler();
		SetFlags(I2C_SR1_TXE);
		adapter.IrqEventHandler();
		SetFlags(I2C_SR1_TXE);
		adapter.IrqEventHandler();
		SetFlags(I2C_SR1_BTF);
		adapter.IrqEventHandler();
	});

	std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	ResultStatus awaitStatus = adapter.Await();
	std::chrono::milliseconds elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
	worker.join();

	EXPECT(awaitStatus == ResultStatus::ok);
	EXPECT(elapsed.count() >= 40);
	EXPECT_EQ(completeCount, 1);
}



// A blocking read with a one-byte register: the register is written, then a repeated START sends the read address.
void TestBlockingReadWithOneByteRegister() {
	ResetCase();
	I2CAdapterF4 adapter(&regs, 42000000);
	simAutoAck = true;
	simAckDevices = { 0x50 };
	simRxQueue = { 0x5A };
	uint8 out[1] = { 0 };

	EXPECT(adapter.ReadByteArray(0x50, 0x10, 1, out, 1) == ResultStatus::ok);
	EXPECT_EQ(simStartCount, 2);
	EXPECT(simWritten == std::vector<uint32>({ 0xA0, 0x10, 0xA1 }));
	EXPECT_EQ(out[0], 0x5A);
}



void TestBlockingReadWithTwoByteRegister() {
	ResetCase();
	I2CAdapterF4 adapter(&regs, 42000000);
	simAutoAck = true;
	simAckDevices = { 0x50 };
	simRxQueue = { 0x5A };
	uint8 out[1] = { 0 };

	EXPECT(adapter.ReadByteArray(0x50, 0x0102, 2, out, 1) == ResultStatus::ok);
	EXPECT_EQ(simStartCount, 2);
	EXPECT(simWritten == std::vector<uint32>({ 0xA0, 0x01, 0x02, 0xA1 }));
	EXPECT_EQ(out[0], 0x5A);
}



void TestBlockingAddressSizeMustBeOneOrTwo() {
	ResetCase();
	I2CAdapterF4 adapter(&regs, 42000000);
	uint8 buffer[1] = { 0x01 };

	EXPECT(adapter.ReadByteArray(0x50, 0x10, 0, buffer, 1) == ResultStatus::invalidParameter);
	EXPECT(adapter.WriteByteArray(0x50, 0x10, 0, buffer, 1) == ResultStatus::invalidParameter);
	EXPECT(adapter.ReadByteArray(0x50, 0x10, 3, buffer, 1) == ResultStatus::invalidParameter);
	EXPECT(adapter.WriteByteArray(0x50, 0x10, 3, buffer, 1) == ResultStatus::invalidParameter);
	EXPECT_EQ(simStartCount, 0);
}



// Overrun and misplaced START or STOP hold the bus until STOP is sent. Arbitration loss releases the bus by itself.
void TestBusErrorsSendStopExceptArbitration() {
	struct Case {
		uint32 flag;
		I2CAdapterF4::Error expected;
		uint32 expectedStops;
	};
	const Case cases[] = {
		{ I2C_SR1_OVR, I2CAdapterF4::Error::OverUnderRun, 1 },
		{ I2C_SR1_BERR, I2CAdapterF4::Error::MisplacedStartStop, 1 },
		{ I2C_SR1_ARLO, I2CAdapterF4::Error::ArbitrationLost, 0 },
	};

	for (const Case &item : cases) {
		ResetCase();
		I2CAdapterF4 adapter(&regs, 42000000);
		WireCallbacks(adapter);
		uint8 data[1] = { 0x01 };

		EXPECT(adapter.ReadByteArrayAsync(0x50, 0x10, 1, data, 1) == ResultStatus::ok);
		SetFlags(item.flag);
		adapter.IrqErrorHandler();

		EXPECT_EQ(simStopCount, item.expectedStops);
		EXPECT_EQ(errors.size(), 1);
		if (!errors.empty()) {
			EXPECT(errors[0] == item.expected);
		}
		EXPECT(adapter.Await() == ResultStatus::error);
	}
}



void TestSetParametersRefusedWhileTransferRuns() {
	ResetCase();
	I2CAdapterF4 adapter(&regs, 42000000);
	uint8 data[1] = { 0x01 };

	EXPECT(adapter.WriteByteArrayAsync(0x50, 0x10, 1, data, 1) == ResultStatus::ok);

	I2CAdapterF4::Parameters params;
	params.speed = I2CAdapterF4::Speed::Fast;
	EXPECT(adapter.SetParameters(params) == ResultStatus::busy);
	EXPECT_EQ(simInitClockSpeed, 0);
}



// An asynchronous read with no register address starts at once: START, device address for read, then the data.
void TestAsyncReadWithoutRegisterStartsAtOnce() {
	ResetCase();
	I2CAdapterF4 adapter(&regs, 42000000);
	WireCallbacks(adapter);
	simRxQueue = { 0x7E };
	uint8 out[1] = { 0 };

	EXPECT(adapter.ReadByteArrayAsync(0x50, 0x00, 0, out, 1) == ResultStatus::ok);

	SetFlags(I2C_SR1_SB);
	adapter.IrqEventHandler();
	EXPECT_EQ(LastWritten(), 0xA1);

	SetFlags(I2C_SR1_ADDR);
	adapter.IrqEventHandler();
	EXPECT_EQ(simStopCount, 1);

	SetFlags(I2C_SR1_RXNE);
	adapter.IrqEventHandler();
	EXPECT_EQ(out[0], 0x7E);
	EXPECT_EQ(completeCount, 1);
}



// FastPlus is refused before anything is set up, so the user's hooks do not run.
void TestFastPlusIsRefusedBeforeHooks() {
	ResetCase();
	I2CAdapterF4 adapter(&regs, 42000000);
	int hookCalls = 0;
	adapter.beforePeripheryInit = [&hookCalls]() {
		hookCalls++;
		return ResultStatus::ok;
	};
	adapter.afterPeripheryInit = [&hookCalls]() {
		hookCalls++;
		return ResultStatus::ok;
	};

	I2CAdapterF4::Parameters params;
	params.speed = I2CAdapterF4::Speed::FastPlus;
	EXPECT(adapter.SetParameters(params) == ResultStatus::notSupported);
	EXPECT_EQ(hookCalls, 0);
	EXPECT_EQ(simInitClockSpeed, 0);
}


} // namespace


int main() {
	TestClockSpeedIsInHertz();
	TestAsyncReadWithRegisterThreeBytes();
	TestAsyncReadOneByte();
	TestAsyncReadTwoBytes();
	TestAsyncReadFiveBytes();
	TestAsyncWriteRegisterThenData();
	TestAsyncWriteRegisterOnly();
	TestAcknowledgeFailureEndsTransfer();
	TestErrorFlagsAreReported();
	TestIrqHandlerDispatchesEventsAndErrors();
	TestReadWithZeroSizeIsRejected();
	TestScanFindsOnlyUserAddresses();
	TestScanStopsWhenListIsFull();
	TestCheckDeviceRetriesRepeatCount();
	TestAwaitWaitsForWorkerThread();
	TestBlockingReadWithOneByteRegister();
	TestBlockingReadWithTwoByteRegister();
	TestBlockingAddressSizeMustBeOneOrTwo();
	TestBusErrorsSendStopExceptArbitration();
	TestSetParametersRefusedWhileTransferRuns();
	TestFastPlusIsRefusedBeforeHooks();
	TestAsyncReadWithoutRegisterStartsAtOnce();

	if (failureCount != 0) {
		std::fprintf(stderr, "%d check(s) failed\n", failureCount);
		return 1;
	}

	std::printf("all I2C checks passed\n");
	return 0;
}
