#include <Drivers/Interface/RegisterBus/SpiRegisterBus.h>
#include <Utilities/Data/RegisterMap/RegisterAccess.h>
#include <Utilities/Result/Result.h>
#include "FakeSpi.h"
#include "SimCheck.h"
#include <cstdio>


// Expected command bytes are hand-computed. 0x3B with the read flag at bit 7 is 0x80 | 0x3B = 0xBB. Its bits reversed (LSB
// first) are 0xDD, and 0x3B reversed is 0xDC.


constexpr auto Measured = RegisterDescriptor<uint16>(0x68, 0x3B).ReadOnly();
constexpr auto Setpoint = RegisterDescriptor<uint16>(0x68, 0x3B);
constexpr auto Wide = RegisterDescriptor<uint8>(0x68, 0x0102).AddressWidth(2);
constexpr auto High = RegisterDescriptor<uint8>(0x68, 0x40);

using DefaultBus = SpiRegisterBus<FakeSpiAdapter>;
using LsbBus = SpiRegisterBus<FakeSpiAdapter, +[] {
	return SpiFramingBuilder().BitOrder(SpiBitOrder::LsbFirst);
}>;
using SixBitBus = SpiRegisterBus<FakeSpiAdapter, +[] {
	return SpiFramingBuilder().AddressBits(6);
}>;


// A read is one window: select, the command byte (read flag set), the data, release.
void TestReadDefaultFraming() {
	FakeSpiAdapter spi;
	spi.reply = { 0x12, 0x34 };
	DefaultBus bus(spi);

	Result<uint16> value = Read<Measured>(bus);
	SimCheck(value.IsOk());
	SimCheck(value.Value() == 0x1234);
	SimCheck(spi.events.size() == 4);
	SimCheck(spi.events[0].kind == FakeSpiKind::Select);
	SimCheck(spi.events[1].kind == FakeSpiKind::Write);
	SimCheck(spi.events[1].bytes.size() == 1);
	SimCheck(spi.events[1].bytes[0] == 0xBB);
	SimCheck(spi.events[2].kind == FakeSpiKind::Read);
	SimCheck(spi.events[3].kind == FakeSpiKind::Release);
}


// A write is one window: the command byte with the read flag clear, then the data.
void TestWriteDefaultFraming() {
	FakeSpiAdapter spi;
	DefaultBus bus(spi);

	SimCheck(Write<Setpoint>(bus, 0x1234) == ResultStatus::ok);
	SimCheck(spi.events.size() == 4);
	SimCheck(spi.events[0].kind == FakeSpiKind::Select);
	SimCheck(spi.events[1].bytes[0] == 0x3B);
	SimCheck(spi.events[2].kind == FakeSpiKind::Write);
	SimCheck(spi.events[2].bytes.size() == 2);
	SimCheck(spi.events[2].bytes[0] == 0x12);
	SimCheck(spi.events[2].bytes[1] == 0x34);
	SimCheck(spi.events[3].kind == FakeSpiKind::Release);
}


// With least significant bit first, the command byte goes out bit-reversed: 0xBB becomes 0xDD.
void TestReadLsbFirst() {
	FakeSpiAdapter spi;
	spi.reply = { 0x00, 0x01 };
	LsbBus bus(spi);

	Result<uint16> value = Read<Measured>(bus);
	SimCheck(value.IsOk());
	SimCheck(value.Value() == 0x0001);
	SimCheck(spi.events[1].bytes[0] == 0xDD);
}


// The write command 0x3B reversed is 0xDC.
void TestWriteLsbFirst() {
	FakeSpiAdapter spi;
	LsbBus bus(spi);

	SimCheck(Write<Setpoint>(bus, 0x0000) == ResultStatus::ok);
	SimCheck(spi.events[1].bytes[0] == 0xDC);
}


// Six address bits: 0x3B fits, and the command is 0x80 | 0x3B = 0xBB. 0x40 does not fit, so nothing is sent.
void TestSixAddressBits() {
	FakeSpiAdapter spi;
	spi.reply = { 0x55, 0x66 };
	SixBitBus bus(spi);

	Result<uint16> value = Read<Measured>(bus);
	SimCheck(value.IsOk());
	SimCheck(value.Value() == 0x5566);
	SimCheck(spi.events[1].bytes[0] == 0xBB);

	FakeSpiAdapter other;
	SixBitBus otherBus(other);
	Result<uint8> rejected = Read<High>(otherBus);
	SimCheck(rejected.IsErr());
	SimCheck(rejected.Error() == ResultStatus::invalidParameter);
	SimCheck(other.events.empty());
}


// A two-byte register address is not supported on SPI: notSupported, and nothing is sent.
void TestTwoByteAddressRejected() {
	FakeSpiAdapter spi;
	DefaultBus bus(spi);

	SimCheck(Write<Wide>(bus, 0x7E) == ResultStatus::notSupported);
	SimCheck(spi.events.empty());
}


int main() {
	TestReadDefaultFraming();
	TestWriteDefaultFraming();
	TestReadLsbFirst();
	TestWriteLsbFirst();
	TestSixAddressBits();
	TestTwoByteAddressRejected();

	if (simFailures != 0) {
		std::printf("TestSpiRegisterBus: %d check(s) failed\n", simFailures);
		return 1;
	}

	std::printf("TestSpiRegisterBus: all checks passed\n");
	return 0;
}
