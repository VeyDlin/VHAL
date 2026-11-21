#include <VHAL.h>
#include <Adapter/I2CAdapter.h>
#include <Adapter/Helper/I2C/I2CMutexAdapter.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>


// Host tests for the base I2CAdapter::Await and for I2CMutexAdapter::ScanAsync. The bus is a fake that finishes a scan at once,
// stays busy, or fails, and the test sets its state from the test thread or from a second thread that stands in for an interrupt.


namespace {


int failureCount = 0;


void Expect(bool condition, const char* expression, int line) {
	if (!condition) {
		failureCount++;
		std::fprintf(stderr, "TestI2CAdapter.cpp:%d: check failed: %s\n", line, expression);
	}
}


void ExpectEqual(long long actual, long long expected, const char* expression, int line) {
	if (actual != expected) {
		failureCount++;
		std::fprintf(stderr, "TestI2CAdapter.cpp:%d: %s is %lld, expected %lld\n", line, expression, actual, expected);
	}
}


#define IA_EXPECT(condition) Expect(static_cast<bool>(condition), #condition, __LINE__)
#define IA_EQ(actual, expected) ExpectEqual(static_cast<long long>(actual), static_cast<long long>(expected), #actual, __LINE__)


// Only the calls the tests need do anything. The rest return ok.
class FakeI2C : public I2CAdapter<int> {
public:
	// When true, ScanAsync fails at once.
	bool scanFails = false;

	// When false, ScanAsync leaves the bus busy, so Await has to wait.
	bool scanCompletes = true;

	int scanCalls = 0;

	// The status the asynchronous starts return. A start that fails never reaches the bus, so the wrapper must release the mutex at once.
	ResultStatus startStatus = ResultStatus::ok;


public:
	FakeI2C() = default;


	void SetState(ResultStatus value) {
		state = value;
	}


	ResultStatus GetState() const {
		return state;
	}


	virtual ResultStatus CheckDevice(uint8, uint16) override {
		return ResultStatus::ok;
	}


	virtual ResultStatus CheckDeviceAsync(uint8, uint16) override {
		return startStatus;
	}


	virtual Result<uint8> Scan(uint8*, uint8) override {
		return static_cast<uint8>(0);
	}


	virtual Result<uint8> ScanAsync(uint8*, uint8) override {
		scanCalls++;
		if (scanFails) {
			return ResultStatus::error;
		}

		state = scanCompletes ? ResultStatus::ready : ResultStatus::busy;
		return static_cast<uint8>(0);
	}


	virtual void IrqEventHandler() override { }
	virtual void IrqErrorHandler() override { }


	virtual ResultStatus WriteByteArray(uint8, uint16, uint8, uint8*, uint32) override {
		return ResultStatus::ok;
	}


	virtual ResultStatus ReadByteArray(uint8, uint16, uint8, uint8*, uint32) override {
		return ResultStatus::ok;
	}


	virtual ResultStatus WriteByteArrayAsync(uint8, uint16, uint8, uint8*, uint32) override {
		return startStatus;
	}


	virtual ResultStatus ReadByteArrayAsync(uint8, uint16, uint8, uint8*, uint32) override {
		return startStatus;
	}


protected:
	virtual ResultStatus Initialization() override {
		return ResultStatus::ok;
	}
};


long long ElapsedMs(std::chrono::steady_clock::time_point start) {
	std::chrono::steady_clock::duration elapsed = std::chrono::steady_clock::now() - start;
	return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
}


void TestAwaitReadyAndError() {
	FakeI2C bus;

	bus.SetState(ResultStatus::ready);
	IA_EQ(bus.Await(), ResultStatus::ok);

	bus.SetState(ResultStatus::error);
	IA_EQ(bus.Await(), ResultStatus::error);
}


// A busy bus that never changes must end the wait at the timeout, and not spin forever.
void TestAwaitTimesOut() {
	FakeI2C bus;
	bus.SetState(ResultStatus::busy);
	bus.SetTimeout(20);

	std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	ResultStatus status = bus.Await();
	long long elapsed = ElapsedMs(start);

	// The tick counts whole milliseconds, so 20 ticks can span a little under 20 real milliseconds.
	IA_EQ(status, ResultStatus::timeout);
	IA_EXPECT(elapsed >= 19);
	IA_EXPECT(elapsed < 5000);
}


// A transfer that outlives the timeout leaves the bus busy: the adapter does not clear the state, because the port may still be
// running it. A later completion is seen by the next Await.
void TestAwaitRetryAfterTimeout() {
	FakeI2C bus;
	bus.SetState(ResultStatus::busy);
	bus.SetTimeout(10);

	IA_EQ(bus.Await(), ResultStatus::timeout);
	IA_EQ(bus.GetState(), ResultStatus::busy);

	bus.SetState(ResultStatus::ready);
	IA_EQ(bus.Await(), ResultStatus::ok);
}


// A completion from another thread, as an interrupt would set it, ends the wait with ok before the timeout. The write is a plain
// store to a volatile member, which is not a synchronising operation under the C++ memory model. That is accepted for this host
// test, because the read in Await is volatile and the tick keeps the loop running. On a target the interrupt is the only writer.
void TestAwaitEndedByAnotherThread() {
	FakeI2C bus;
	bus.SetState(ResultStatus::busy);
	// Long enough that the interrupt always wins, short enough that a broken wait, which runs to this limit, ends in seconds.
	bus.SetTimeout(500);

	std::thread interrupt([&bus]() {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		bus.SetState(ResultStatus::ready);
	});

	ResultStatus status = bus.Await();
	interrupt.join();

	IA_EQ(status, ResultStatus::ok);
}


// The mutex is taken and released once for a scan that fails at once.
void TestScanAsyncFailureUnlocks() {
	I2CMutexAdapter<FakeI2C> bus;
	int locks = 0;
	int unlocks = 0;

	bus.onMutex = [&](bool isLock) {
		if (isLock) {
			locks++;
		} else {
			unlocks++;
		}
	};

	bus.scanFails = true;
	uint8 buffer[1] = { 0 };
	Result<uint8> result = bus.ScanAsync(buffer, 1);

	IA_EXPECT(result.IsErr());
	IA_EQ(locks, 1);
	IA_EQ(unlocks, 1);
}


// The mutex is released when the wait for the scan times out.
void TestScanAsyncTimeoutUnlocks() {
	I2CMutexAdapter<FakeI2C> bus;
	int locks = 0;
	int unlocks = 0;

	bus.onMutex = [&](bool isLock) {
		if (isLock) {
			locks++;
		} else {
			unlocks++;
		}
	};

	bus.scanCompletes = false;
	bus.SetTimeout(20);
	uint8 buffer[1] = { 0 };
	Result<uint8> result = bus.ScanAsync(buffer, 1);

	IA_EXPECT(result.IsErr());
	IA_EQ(locks, 1);
	IA_EQ(unlocks, 1);
}


void TestScanAsyncSuccessUnlocks() {
	I2CMutexAdapter<FakeI2C> bus;
	int locks = 0;
	int unlocks = 0;

	bus.onMutex = [&](bool isLock) {
		if (isLock) {
			locks++;
		} else {
			unlocks++;
		}
	};

	uint8 buffer[1] = { 0 };
	Result<uint8> result = bus.ScanAsync(buffer, 1);

	IA_EXPECT(result.IsOk());
	IA_EQ(locks, 1);
	IA_EQ(unlocks, 1);
}


// The mutex is released when an asynchronous check fails to start, as it is for a scan.
void TestCheckDeviceAsyncStartFailureUnlocks() {
	I2CMutexAdapter<FakeI2C> bus;
	int locks = 0;
	int unlocks = 0;

	bus.onMutex = [&](bool isLock) {
		if (isLock) {
			locks++;
		} else {
			unlocks++;
		}
	};

	bus.startStatus = ResultStatus::error;
	ResultStatus status = bus.CheckDeviceAsync(0x42, 1);

	IA_EQ(status, ResultStatus::error);
	IA_EQ(locks, 1);
	IA_EQ(unlocks, 1);
}


// The mutex is released when an asynchronous write fails to start.
void TestWriteAsyncStartFailureUnlocks() {
	I2CMutexAdapter<FakeI2C> bus;
	int locks = 0;
	int unlocks = 0;

	bus.onMutex = [&](bool isLock) {
		if (isLock) {
			locks++;
		} else {
			unlocks++;
		}
	};

	bus.startStatus = ResultStatus::error;
	uint8 data[1] = { 0 };
	ResultStatus status = bus.WriteByteArrayAsync(0x42, 0, 1, data, 1);

	IA_EQ(status, ResultStatus::error);
	IA_EQ(locks, 1);
	IA_EQ(unlocks, 1);
}


// The mutex is released when an asynchronous read fails to start.
void TestReadAsyncStartFailureUnlocks() {
	I2CMutexAdapter<FakeI2C> bus;
	int locks = 0;
	int unlocks = 0;

	bus.onMutex = [&](bool isLock) {
		if (isLock) {
			locks++;
		} else {
			unlocks++;
		}
	};

	bus.startStatus = ResultStatus::error;
	uint8 data[1] = { 0 };
	ResultStatus status = bus.ReadByteArrayAsync(0x42, 0, 1, data, 1);

	IA_EQ(status, ResultStatus::error);
	IA_EQ(locks, 1);
	IA_EQ(unlocks, 1);
}


}


int main() {
	// The stand-in for SysTick: one tick per millisecond, from a thread the optimiser cannot see into.
	std::atomic<bool> tickRunning = true;
	std::thread sysTick([&tickRunning]() {
		while (tickRunning.load()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			System::tickLow = System::tickLow + 1;
		}
	});

	TestAwaitReadyAndError();
	TestAwaitTimesOut();
	TestAwaitRetryAfterTimeout();
	TestAwaitEndedByAnotherThread();
	TestScanAsyncFailureUnlocks();
	TestScanAsyncTimeoutUnlocks();
	TestScanAsyncSuccessUnlocks();
	TestCheckDeviceAsyncStartFailureUnlocks();
	TestWriteAsyncStartFailureUnlocks();
	TestReadAsyncStartFailureUnlocks();

	tickRunning.store(false);
	sysTick.join();

	if (failureCount == 0) {
		std::printf("TestI2CAdapter: all checks passed\n");
		return 0;
	}

	std::printf("TestI2CAdapter: %d check(s) failed\n", failureCount);
	return 1;
}
