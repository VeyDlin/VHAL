#include <VHAL.h>
#include <Adapter/UARTAdapter.h>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <utility>

// Host tests for the rules of the UART base class that do not need registers: an asynchronous call with size 0 is refused,
// Read<T>() moves exactly one value, and the transfer counter getters return 32-bit values. A fake port records each byte-level call.


namespace {


int failureCount = 0;


void Expect(bool condition, const char* expression, int line) {
	if (!condition) {
		failureCount++;
		std::fprintf(stderr, "TestUARTAdapter.cpp:%d: check failed: %s\n", line, expression);
	}
}


void ExpectEqual(long long actual, long long expected, const char* expression, int line) {
	if (actual != expected) {
		failureCount++;
		std::fprintf(stderr, "TestUARTAdapter.cpp:%d: %s is %lld, expected %lld\n", line, expression, actual, expected);
	}
}


#define UA_EXPECT(condition) Expect(static_cast<bool>(condition), #condition, __LINE__)
#define UA_EQ(actual, expected) ExpectEqual(static_cast<long long>(actual), static_cast<long long>(expected), #actual, __LINE__)


struct FakeUartHandle {
	uint32 unused = 0;
};


class FakeUart: public UARTAdapter<FakeUartHandle> {
public:
	uint32 writeByteArrayCalls = 0;
	uint32 readByteArrayCalls = 0;
	uint32 writeByteArrayAsyncCalls = 0;
	uint32 readByteArrayAsyncCalls = 0;
	uint32 lastSize = 0;


public:
	FakeUart(FakeUartHandle *handle): UARTAdapter(handle) { }


	void IrqHandler() override { }
	void AbortReceive() override { }
	void AbortTransmit() override { }


	// The base class keeps the counters protected; the fake sets them to check their width.
	void SetCounters(uint32 value) {
		rxDataCounter = value;
		txDataCounter = value;
	}


protected:
	ResultStatus Initialization() override {
		return ResultStatus::ok;
	}


	ResultStatus WriteByteArray(uint8*, uint32 size) override {
		writeByteArrayCalls++;
		lastSize = size;
		return ResultStatus::ok;
	}


	ResultStatus ReadByteArray(uint8 *buffer, uint32 size) override {
		readByteArrayCalls++;
		lastSize = size;
		for (uint32 index = 0; index < size; index++) {
			buffer[index] = static_cast<uint8>(0x11 * (index + 1));
		}
		return ResultStatus::ok;
	}


	ResultStatus WriteByteArrayAsync(uint8*, uint32 size) override {
		writeByteArrayAsyncCalls++;
		lastSize = size;
		return ResultStatus::ok;
	}


	ResultStatus ReadByteArrayAsync(uint8*, uint32 size) override {
		readByteArrayAsyncCalls++;
		lastSize = size;
		return ResultStatus::ok;
	}
};


void TestAsyncSizeZeroIsRefused() {
	FakeUartHandle handle;
	FakeUart uart(&handle);
	uint8 buffer[4] = { 0 };

	UA_EQ(uart.WriteArrayAsync(buffer, 0), ResultStatus::invalidParameter);
	UA_EQ(uart.WriteArrayAsync(static_cast<const uint8*>(buffer), 0), ResultStatus::invalidParameter);
	UA_EQ(uart.WriteStringAsync(""), ResultStatus::invalidParameter);
	UA_EQ(uart.ReadArrayAsync(buffer, 0), ResultStatus::invalidParameter);
	UA_EQ(uart.writeByteArrayAsyncCalls, 0);
	UA_EQ(uart.readByteArrayAsyncCalls, 0);

	UA_EQ(uart.WriteArrayAsync(buffer, 4), ResultStatus::ok);
	UA_EQ(uart.writeByteArrayAsyncCalls, 1);
	UA_EQ(uart.lastSize, 4);

	UA_EQ(uart.ReadArrayAsync(buffer, 2), ResultStatus::ok);
	UA_EQ(uart.lastSize, 2);
}


void TestSingleValueAsyncMovesOneObject() {
	FakeUartHandle handle;
	FakeUart uart(&handle);
	uint32 value = 0;

	UA_EQ(uart.WriteAsync(value), ResultStatus::ok);
	UA_EQ(uart.lastSize, sizeof(uint32));

	UA_EQ(uart.ReadAsync(value), ResultStatus::ok);
	UA_EQ(uart.lastSize, sizeof(uint32));
}


void TestReadMovesOneValue() {
	FakeUartHandle handle;
	FakeUart uart(&handle);

	Result<uint32> result = uart.Read<uint32>();
	UA_EQ(uart.readByteArrayCalls, 1);
	UA_EQ(uart.lastSize, sizeof(uint32));
	UA_EXPECT(result.IsOk());
	// The fake writes 0x11, 0x22, 0x33, 0x44 in order; the value is little-endian.
	UA_EQ(result.Value(), 0x44332211u);
}


// Checks the counter getters, not the port increments: the test sets the counters directly, so a port that truncates while
// counting is not covered here.
void TestCounterGettersReturn32Bit() {
	FakeUartHandle handle;
	FakeUart uart(&handle);

	uart.SetCounters(70000);
	UA_EQ(uart.GetRxDataCounter(), 70000);
	UA_EQ(uart.GetTxDataCounter(), 70000);

	uart.SetCounters(0xFFFFFFFFu);
	UA_EQ(uart.GetRxDataCounter(), 0xFFFFFFFFu);
	UA_EQ(uart.GetTxDataCounter(), 0xFFFFFFFFu);

	static_assert(sizeof(decltype(std::declval<FakeUart&>().GetRxDataCounter())) == sizeof(uint32));
	static_assert(sizeof(decltype(std::declval<FakeUart&>().GetTxDataCounter())) == sizeof(uint32));
}


}


int main() {
	TestAsyncSizeZeroIsRefused();
	TestSingleValueAsyncMovesOneObject();
	TestReadMovesOneValue();
	TestCounterGettersReturn32Bit();

	if (failureCount != 0) {
		std::fprintf(stderr, "TestUARTAdapter: %d check(s) failed\n", failureCount);
		return 1;
	}

	std::printf("TestUARTAdapter: all checks passed\n");
	return 0;
}
