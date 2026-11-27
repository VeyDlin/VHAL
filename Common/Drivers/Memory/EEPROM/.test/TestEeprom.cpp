#include <VHAL.h>
#include <Drivers/Memory/EEPROM/Data.h>
#include <Drivers/Memory/EEPROM/IC/24AA02E48.h>
#include <Drivers/Memory/EEPROM/IC/AT24C256C.h>
#include <cstdio>
#include <cstring>
#include <type_traits>

// Host tests for the EEPROM driver against a fake I2C bus with a 24xx chip model (shim/VHAL.h). The chip models the page wrap,
// the write cycle with NACK while busy, the 7-bit device address, and the capacity. Each check names the defect it covers.


namespace {


int failureCount = 0;


void Expect(bool condition, const char* expression, int line) {
	if (!condition) {
		failureCount++;
		std::fprintf(stderr, "TestEeprom.cpp:%d: check failed: %s\n", line, expression);
	}
}


void ExpectEqual(long long actual, long long expected, const char* expression, int line) {
	if (actual != expected) {
		failureCount++;
		std::fprintf(stderr, "TestEeprom.cpp:%d: %s is %lld, expected %lld\n", line, expression, actual, expected);
	}
}


#define EE_EXPECT(condition) Expect(static_cast<bool>(condition), #condition, __LINE__)
#define EE_EQ(actual, expected) ExpectEqual(static_cast<long long>(actual), static_cast<long long>(expected), #actual, __LINE__)


// A Result<bool> that is an error counts as false.
bool IsTrue(Result<bool> result) {
	return result.IsOk() && result.Value();
}


bool IsFalse(Result<bool> result) {
	return result.IsOk() && !result.Value();
}


struct Fill62 {
	uint8 bytes[62];
};


struct Fill256 {
	uint8 bytes[256];
};


struct Fill32K {
	uint8 bytes[32768];
};


// The compile-time rules of Data<T>: no conversion to the value or to a Result, no assignment from the value, and no
// increment or decrement. Each one would hide a bus error or make a read-modify-write look atomic.
using DataU32 = EEPROM::Data<uint32>;


template<typename T>
concept CanPostIncrement = requires(T d) { d++; };

template<typename T>
concept CanPreIncrement = requires(T d) { ++d; };

template<typename T>
concept CanPostDecrement = requires(T d) { d--; };

template<typename T>
concept CanPreDecrement = requires(T d) { --d; };


static_assert(!std::is_convertible_v<DataU32, uint32>, "Data<T> must not convert to T");
static_assert(!std::is_assignable_v<DataU32&, uint32>, "Data<T> must not assign a value");
static_assert(!CanPostIncrement<DataU32>, "Data<T> must not have a post-increment");
static_assert(!CanPreIncrement<DataU32>, "Data<T> must not have a pre-increment");
static_assert(!CanPostDecrement<DataU32>, "Data<T> must not have a post-decrement");
static_assert(!CanPreDecrement<DataU32>, "Data<T> must not have a pre-decrement");


// The 7-bit address rule: the chip class gives the 7-bit address 0x50 with the pins in bits 2:0, and the bus refuses the
// 8-bit control byte. Capacity and word address come from the chip model.
void TestAddressRule() {
	Chip24xx chip(0x50, 32768, 64, 2, 5);
	AI2C bus(chip);
	EEPROM::AT24C256C eeprom(bus);

	EE_EQ(eeprom.IsMemoryReady(), ResultStatus::ok);
	EE_EQ(bus.lastDevice, 0x50);

	Chip24xx pinnedChip(0x54, 32768, 64, 2, 5);
	AI2C pinnedBus(pinnedChip);
	EEPROM::AT24C256C pinned(pinnedBus, false, false, true);
	EE_EQ(pinned.IsMemoryReady(), ResultStatus::ok);
	EE_EQ(pinnedBus.lastDevice, 0x54);

	// A bus check, not a driver check: the shim refuses an 8-bit address as the ESP32 port does. The driver's own refusal is in
	// TestBadAddressIsNotSent.
	EE_EQ(bus.CheckDevice(0xA0), ResultStatus::badAddress);

	uint8 value = 0x5A;
	EE_EQ(eeprom.WriteMemory(&value, 1, 0x1234), ResultStatus::ok);
	EE_EQ(bus.lastDevice, 0x50);
	EE_EQ(bus.lastAddressBytes, 2);
	EE_EQ(chip.memory[0x1234], 0x5A);

	Chip24xx smallChip(0x50, 256, 8, 1, 5);
	AI2C smallBus(smallChip);
	EEPROM::Chip24AA02E48 small(smallBus);

	EE_EQ(small.IsMemoryReady(), ResultStatus::ok);
	EE_EQ(smallBus.lastDevice, 0x50);

	uint8 smallValue = 0x33;
	EE_EQ(small.WriteMemory(&smallValue, 1, 0x10), ResultStatus::ok);
	EE_EQ(smallBus.lastAddressBytes, 1);
	EE_EQ(smallChip.memory[0x10], 0x33);
}


// The geometry of the chip classes: the 24AA02E48 is 256 bytes of 8-byte pages with a one-byte address, and the AT24C256C is
// 32768 bytes of 64-byte pages with a two-byte address. A record that fills the device is accepted, and one byte more is refused.
void TestCapacity() {
	Chip24xx smallChip(0x50, 256, 8, 1, 5);
	AI2C smallBus(smallChip);
	EEPROM::Chip24AA02E48 small(smallBus);

	EEPROM::Data<Fill256> full(small);
	EE_EQ(full.GetAllocation(), ResultStatus::ok);
	EEPROM::Data<uint8> extra(small);
	EE_EQ(extra.GetAllocation(), ResultStatus::outOfRange);

	Chip24xx chip(0x50, 32768, 64, 2, 5);
	AI2C bus(chip);
	EEPROM::AT24C256C eeprom(bus);

	EEPROM::Data<Fill32K> wholeDevice(eeprom);
	EE_EQ(wholeDevice.GetAllocation(), ResultStatus::ok);
	EEPROM::Data<uint8> pastEnd(eeprom);
	EE_EQ(pastEnd.GetAllocation(), ResultStatus::outOfRange);
}


// A full device returns outOfRange from every call on the record, and the address is not read from the error value.
void TestFullDeviceReturnsError() {
	Chip24xx smallChip(0x50, 256, 8, 1, 5);
	AI2C smallBus(smallChip);
	EEPROM::Chip24AA02E48 small(smallBus);

	EEPROM::Data<Fill256> full(small);
	EEPROM::Data<uint32> late(small);

	EE_EQ(late.GetAllocation(), ResultStatus::outOfRange);

	Result<uint32> read = late.Get();
	EE_EXPECT(read.IsErr());
	EE_EQ(read.Error(), ResultStatus::outOfRange);

	EE_EXPECT(late.GetAddress().IsErr());
	EE_EQ(late.Set(1), ResultStatus::outOfRange);
	EE_EQ(smallChip.acceptedWrites, 0);
	EE_EQ(full.GetAllocation(), ResultStatus::ok);
}


// A default record is not placed in any device: every call returns noInit, and nothing reaches the bus.
void TestDefaultRecordIsNotPlaced() {
	DataU32 blank;

	EE_EQ(blank.GetAllocation(), ResultStatus::noInit);
	EE_EQ(blank.GetAddress().Error(), ResultStatus::noInit);
	EE_EQ(DataU32::dataSize, 4);

	Result<uint32> read = blank.Get();
	EE_EXPECT(read.IsErr());
	EE_EQ(read.Error(), ResultStatus::noInit);
	EE_EQ(blank.Set(1), ResultStatus::noInit);
}


// A write that crosses a 64-byte page edge is split at the edge. The 4-byte word at 62 must land at 62..65, and the bytes at 0
// and 1 must keep their value. Without the split the 24xx model wraps the tail into the start of the page.
void TestPageWrapSplitsAtEdge() {
	Chip24xx chip(0x50, 32768, 64, 2, 5);
	AI2C bus(chip);
	EEPROM::AT24C256C eeprom(bus);

	EEPROM::Data<Fill62> pad(eeprom);
	EEPROM::Data<uint32> word(eeprom);
	EE_EQ(word.GetAddress().Value(), 62);

	Fill62 pattern{};
	for (uint32 i = 0; i < 62; i++) {
		pattern.bytes[i] = 0xAA;
	}

	EE_EQ(pad.Set(pattern), ResultStatus::ok);

	uint32 value = 0x11223344;
	uint8 expected[4];
	std::memcpy(expected, &value, sizeof(value));

	EE_EQ(word.Set(value), ResultStatus::ok);

	EE_EQ(chip.memory[0], 0xAA);
	EE_EQ(chip.memory[1], 0xAA);

	for (uint32 i = 0; i < 4; i++) {
		EE_EQ(chip.memory[62 + i], expected[i]);
	}

	EE_EQ(chip.memory[66], 0xFF);
}


// A raw write of 150 bytes from address 10 covers three pages: 54 bytes, 64 bytes and 32 bytes. Each lands at its own address.
void TestRawWriteSplitsAcrossPages() {
	Chip24xx chip(0x50, 32768, 64, 2, 5);
	AI2C bus(chip);
	EEPROM::AT24C256C eeprom(bus);

	uint8 buffer[150];
	for (uint32 i = 0; i < 150; i++) {
		buffer[i] = static_cast<uint8>(i + 1);
	}

	uint32 before = chip.acceptedWrites;
	EE_EQ(eeprom.WriteMemory(buffer, 150, 10), ResultStatus::ok);
	EE_EQ(chip.acceptedWrites - before, 3);

	for (uint32 i = 0; i < 150; i++) {
		EE_EQ(chip.memory[10 + i], buffer[i]);
	}

	EE_EQ(chip.memory[9], 0xFF);
	EE_EQ(chip.memory[160], 0xFF);
}


// The 24AA02E48 has 8-byte pages, so a 10-byte write from address 4 splits into 4 bytes and 6 bytes.
void TestSmallPageSplit() {
	Chip24xx chip(0x50, 256, 8, 1, 5);
	AI2C bus(chip);
	EEPROM::Chip24AA02E48 small(bus);

	uint8 buffer[10];
	for (uint32 i = 0; i < 10; i++) {
		buffer[i] = static_cast<uint8>(0x40 + i);
	}

	uint32 before = chip.acceptedWrites;
	EE_EQ(small.WriteMemory(buffer, 10, 4), ResultStatus::ok);
	EE_EQ(chip.acceptedWrites - before, 2);

	for (uint32 i = 0; i < 10; i++) {
		EE_EQ(chip.memory[4 + i], buffer[i]);
	}

	EE_EQ(chip.memory[3], 0xFF);
	EE_EQ(chip.memory[14], 0xFF);
}


// The write cycle: after a write the driver polls the chip until it acknowledges, so the next write is not NACKed. The chip
// NACKs a write made while it is busy. A chip that stays busy past the datasheet time gives a timeout.
void TestWriteCycleWait() {
	Chip24xx chip(0x50, 32768, 64, 2, 5);
	AI2C bus(chip);
	EEPROM::AT24C256C eeprom(bus);

	EEPROM::Data<uint32> first(eeprom);
	EEPROM::Data<uint32> second(eeprom);

	uint64 start = System::GetMs();
	EE_EQ(first.Set(1), ResultStatus::ok);
	EE_EQ(second.Set(2), ResultStatus::ok);
	EE_EQ(first.Set(3), ResultStatus::ok);

	EE_EXPECT(System::GetMs() - start >= 15);

	uint8 raw = 0x77;
	EE_EQ(bus.WriteByteArray(0x50, 0, 2, &raw, 1), ResultStatus::ok);
	EE_EQ(bus.WriteByteArray(0x50, 0, 2, &raw, 1), ResultStatus::nack);

	System::DelayMs(10);
	chip.writeCycleMs = 50;
	EE_EQ(first.Set(9), ResultStatus::timeout);
}


// The ACK poll after a write: one probe before the write finds the chip ready. The write leaves the chip busy for 5 ms, and the
// poll probes at 0, 1, 2, 3, 4 and 5 ms after the write. The sixth of them, at the deadline, is the first that acknowledges.
// That is seven probes in all, and the write takes 5 ms.
void TestAckPollCount() {
	Chip24xx chip(0x50, 32768, 64, 2, 5);
	AI2C bus(chip);
	EEPROM::AT24C256C eeprom(bus);

	EEPROM::Data<uint32> record(eeprom);

	uint64 start = System::GetMs();
	EE_EQ(record.Set(1), ResultStatus::ok);
	EE_EQ(System::GetMs() - start, 5);
	EE_EQ(bus.probeCount, 7);
	EE_EQ(bus.writeCallCount, 1);
}


// A write made while the chip is still in an earlier write cycle waits for that cycle first. The first attempt is accepted, so
// the retries are not used, and the record reads back.
void TestBusyChipAtCallTimeIsWaitedOut() {
	Chip24xx chip(0x50, 32768, 64, 2, 5);
	AI2C bus(chip);
	EEPROM::AT24C256C eeprom(bus);

	EEPROM::Data<uint32> record(eeprom);
	eeprom.retry = 3;

	uint8 earlier = 0x77;
	EE_EQ(bus.WriteByteArray(0x50, 100, 2, &earlier, 1), ResultStatus::ok);

	uint32 before = bus.writeCallCount;
	EE_EQ(record.Set(0x0A0B0C0D), ResultStatus::ok);
	EE_EQ(bus.writeCallCount - before, 1);

	Result<uint32> stored = record.Get();
	EE_EXPECT(stored.IsOk());
	EE_EQ(stored.Value(), 0x0A0B0C0D);
}


// A chip that stays busy past the driver's 5 ms write cycle gives timeout, and the write is not repeated: a repeat would wait the
// same time again.
void TestTimeoutIsNotRetried() {
	Chip24xx chip(0x50, 32768, 64, 2, 50);
	AI2C bus(chip);
	EEPROM::AT24C256C eeprom(bus);

	EEPROM::Data<uint32> record(eeprom);
	eeprom.retry = 3;

	uint32 before = bus.writeCallCount;
	EE_EQ(record.Set(1), ResultStatus::timeout);
	EE_EQ(bus.writeCallCount - before, 1);
}


// A geometry the adapter cannot address is invalidParameter before the bus is called. A zero page size divides by zero, a
// capacity past 65536 bytes wraps the 16-bit word address, and a one-byte word address reaches 256 bytes only.
void TestGeometryIsChecked() {
	Chip24xx chip(0x50, 256, 8, 1, 5);
	AI2C bus(chip);

	EEPROM::Geometry noPage;
	noPage.capacity = 256;
	noPage.pageSize = 0;
	noPage.addressBytes = 1;
	noPage.writeCycleMs = 5;
	EEPROM::Eeprom zeroPage(bus, 0x50, noPage);

	uint8 value = 0x5A;
	EE_EQ(zeroPage.IsMemoryReady(), ResultStatus::invalidParameter);
	EE_EQ(zeroPage.ReadMemory(&value, 1, 0), ResultStatus::invalidParameter);
	EE_EQ(zeroPage.WriteMemory(&value, 1, 0), ResultStatus::invalidParameter);
	EE_EQ(bus.probeCount, 0);
	EE_EQ(bus.readCallCount, 0);
	EE_EQ(bus.writeCallCount, 0);

	Chip24xx bigChip(0x50, 131072, 64, 2, 5);
	AI2C bigBus(bigChip);

	EEPROM::Geometry tooBig;
	tooBig.capacity = 131072;
	tooBig.pageSize = 64;
	tooBig.addressBytes = 2;
	tooBig.writeCycleMs = 5;
	EEPROM::Eeprom wide(bigBus, 0x50, tooBig);

	EE_EQ(wide.WriteMemory(&value, 1, 65536), ResultStatus::invalidParameter);
	EE_EQ(bigChip.acceptedWrites, 0);
	EE_EQ(bigChip.memory[0], 0xFF);

	EEPROM::Geometry narrow;
	narrow.capacity = 512;
	narrow.pageSize = 8;
	narrow.addressBytes = 1;
	narrow.writeCycleMs = 5;
	EEPROM::Eeprom narrowEeprom(bus, 0x50, narrow);

	EE_EQ(narrowEeprom.WriteMemory(&value, 1, 0), ResultStatus::invalidParameter);
	EE_EQ(bus.writeCallCount, 0);
}


// The driver refuses a device address of 0x80 or more before the bus is called, so an 8-bit control byte such as 0xA0 is never
// sent, whatever the port does with it.
void TestBadAddressIsNotSent() {
	Chip24xx chip(0x50, 32768, 64, 2, 5);
	AI2C bus(chip);
	EEPROM::AT24C256C eeprom(bus);

	eeprom.SetAddress(0xA0);

	uint8 value = 0x5A;
	EE_EQ(eeprom.IsMemoryReady(), ResultStatus::badAddress);
	EE_EQ(eeprom.ReadMemory(&value, 1, 0), ResultStatus::badAddress);
	EE_EQ(eeprom.WriteMemory(&value, 1, 0), ResultStatus::badAddress);
	EE_EQ(bus.probeCount, 0);
	EE_EQ(bus.readCallCount, 0);
	EE_EQ(bus.writeCallCount, 0);
	EE_EQ(chip.acceptedWrites, 0);
}


// An Eeprom cannot be copied, because two copies would hand out the same bytes. A Data<T> is not made from an Eeprom by
// conversion, because that would reserve space without a name.
void TestDriverCopyAndConversion() {
	constexpr bool copyable = std::is_copy_constructible_v<EEPROM::AT24C256C>;
	constexpr bool assignable = std::is_copy_assignable_v<EEPROM::AT24C256C>;
	constexpr bool converts = std::is_convertible_v<EEPROM::Eeprom &, DataU32>;

	EE_EXPECT(!copyable);
	EE_EXPECT(!assignable);
	EE_EXPECT(!converts);
}


// Retries: Data<T>::Set retries a NACKed write up to retry times, and returns the last status when they are used up.
void TestRetryReturnsLastStatus() {
	Chip24xx chip(0x50, 32768, 64, 2, 5);
	AI2C bus(chip);
	EEPROM::AT24C256C eeprom(bus);

	EEPROM::Data<uint32> record(eeprom);

	eeprom.retry = 3;
	bus.forcedNacks = 2;
	uint32 before = bus.writeCallCount;
	EE_EQ(record.Set(7), ResultStatus::ok);
	EE_EQ(bus.writeCallCount - before, 3);

	eeprom.retry = 2;
	bus.forcedNacks = 2;
	EE_EQ(record.Set(8), ResultStatus::nack);
}


// Raw reads and writes are checked against the capacity: a range that runs past the last byte is outOfRange, and a null
// buffer with a size is invalidParameter. A zero-size call does nothing.
void TestRawBounds() {
	Chip24xx chip(0x50, 32768, 64, 2, 5);
	AI2C bus(chip);
	EEPROM::AT24C256C eeprom(bus);

	uint8 buffer[4] = { 0, 0, 0, 0 };

	EE_EQ(eeprom.ReadMemory(buffer, 4, 32766), ResultStatus::outOfRange);
	EE_EQ(eeprom.ReadMemory(buffer, 2, 32767), ResultStatus::outOfRange);
	EE_EQ(eeprom.ReadMemory(buffer, 2, 32766), ResultStatus::ok);
	EE_EQ(eeprom.ReadMemory(buffer, 0, 40000), ResultStatus::ok);
	EE_EQ(eeprom.ReadMemory(nullptr, 2, 0), ResultStatus::invalidParameter);

	EE_EQ(eeprom.WriteMemory(buffer, 3, 32767), ResultStatus::outOfRange);
	EE_EQ(eeprom.WriteMemory(buffer, 1, 32767), ResultStatus::ok);
	EE_EQ(eeprom.WriteMemory(nullptr, 2, 0), ResultStatus::invalidParameter);
	EE_EQ(chip.acceptedWrites, 1);

	Chip24xx smallChip(0x50, 256, 8, 1, 5);
	AI2C smallBus(smallChip);
	EEPROM::Chip24AA02E48 small(smallBus);

	EE_EQ(small.WriteMemory(buffer, 2, 255), ResultStatus::outOfRange);
	EE_EQ(small.ReadMemory(buffer, 2, 255), ResultStatus::outOfRange);
}


// Data<T> comparisons compare the stored value with the argument, and report a bus error instead of a false.
void TestDataComparisons() {
	Chip24xx chip(0x50, 32768, 64, 2, 5);
	AI2C bus(chip);
	EEPROM::AT24C256C eeprom(bus);

	EEPROM::Data<int32> number(eeprom);
	EE_EQ(number.Set(5), ResultStatus::ok);

	EE_EXPECT(IsTrue(number > 1));
	EE_EXPECT(IsTrue(number < 10));
	EE_EXPECT(IsFalse(number < 5));
	EE_EXPECT(IsFalse(number > 5));
	EE_EXPECT(IsTrue(number == 5));
	EE_EXPECT(IsFalse(number != 5));
	EE_EXPECT(IsTrue(number != 6));

	eeprom.retry = 1;
	bus.forcedNacks = 1;
	Result<bool> failed = (number == 5);
	EE_EXPECT(failed.IsErr());
	EE_EQ(failed.Error(), ResultStatus::nack);
}


// Data<T> read and write errors reach the caller. A NACK on the read is an error result, not a zero value.
void TestDataErrorsReturned() {
	Chip24xx chip(0x50, 32768, 64, 2, 5);
	AI2C bus(chip);
	EEPROM::AT24C256C eeprom(bus);

	EEPROM::Data<uint32> record(eeprom);
	EE_EQ(record.Set(0x01020304u), ResultStatus::ok);

	eeprom.retry = 1;
	bus.forcedNacks = 1;
	Result<uint32> read = record.Get();
	EE_EXPECT(read.IsErr());
	EE_EQ(read.Error(), ResultStatus::nack);

	Result<uint32> again = record.Get();
	EE_EXPECT(again.IsOk());
	EE_EQ(again.Value(), 0x01020304u);
}


}


int main() {
	TestAddressRule();
	TestCapacity();
	TestFullDeviceReturnsError();
	TestDefaultRecordIsNotPlaced();
	TestPageWrapSplitsAtEdge();
	TestRawWriteSplitsAcrossPages();
	TestSmallPageSplit();
	TestWriteCycleWait();
	TestRetryReturnsLastStatus();
	TestRawBounds();
	TestDataComparisons();
	TestDataErrorsReturned();
	TestAckPollCount();
	TestBusyChipAtCallTimeIsWaitedOut();
	TestTimeoutIsNotRetried();
	TestGeometryIsChecked();
	TestBadAddressIsNotSent();
	TestDriverCopyAndConversion();

	if (failureCount == 0) {
		std::printf("TestEeprom: all checks passed\n");
		return 0;
	}

	std::printf("TestEeprom: %d check(s) failed\n", failureCount);
	return 1;
}
