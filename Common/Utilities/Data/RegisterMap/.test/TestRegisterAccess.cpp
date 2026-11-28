#include <Utilities/Data/RegisterMap/RegisterAccess.h>
#include "FakeBus.h"
#include "SimCheck.h"
#include <Utilities/Result/Result.h>
#include <cstdio>


// Expected values are hand-computed from the register layouts below (see the comments), not from the code under test.


constexpr auto ChargeControl = RegisterDescriptor<uint8>(0x09, 0x01).ReadWrite();
constexpr auto ChargeDisable = BitField<1>(ChargeControl, 3);
constexpr auto ModeField = BitField<2>(ChargeControl, 5);
constexpr auto Measured = RegisterDescriptor<uint16>(0x68, 0x3B).ReadOnly();
constexpr auto MeasuredLittle = RegisterDescriptor<uint16>(0x68, 0x3B).Order(ByteOrder::little).ReadOnly();
constexpr auto Signed = RegisterDescriptor<int16>(0x68, 0x40).ReadOnly();
constexpr auto Wide = RegisterDescriptor<uint8>(0x50, 0x0102).AddressWidth(2);
constexpr auto Setpoint = RegisterDescriptor<uint16>(0x68, 0x20);
constexpr auto Command = RegisterDescriptor<uint8>(0x68, 0x30).WriteOnly();


// Reads one byte and checks the device, the address, the width and the bytes of the transaction.
void TestReadOneByte() {
	FakeRegisterBus bus;
	bus.memory[0x01] = 0x5A;

	Result<uint8> value = Read<ChargeControl>(bus);
	SimCheck(value.IsOk());
	SimCheck(value.Value() == 0x5A);
	SimCheck(bus.log.size() == 1);
	SimCheck(bus.log[0].isRead);
	SimCheck(bus.log[0].where.device == 0x09);
	SimCheck(bus.log[0].where.address == 0x01);
	SimCheck(bus.log[0].where.addressWidth == 1);
	SimCheck(bus.log[0].bytes.size() == 1);
}


// Two bytes at 0x3B and 0x3C, big-endian: 0x12 then 0x34 is 0x1234.
void TestReadBigEndian() {
	FakeRegisterBus bus;
	bus.memory[0x3B] = 0x12;
	bus.memory[0x3C] = 0x34;

	Result<uint16> value = Read<Measured>(bus);
	SimCheck(value.IsOk());
	SimCheck(value.Value() == 0x1234);
	SimCheck(bus.log[0].bytes.size() == 2);
	SimCheck(bus.log[0].bytes[0] == 0x12);
	SimCheck(bus.log[0].bytes[1] == 0x34);
}


// The same two bytes, little-endian: 0x34 is the low byte, so the value is 0x3412.
void TestReadLittleEndian() {
	FakeRegisterBus bus;
	bus.memory[0x3B] = 0x12;
	bus.memory[0x3C] = 0x34;

	Result<uint16> value = Read<MeasuredLittle>(bus);
	SimCheck(value.IsOk());
	SimCheck(value.Value() == 0x3412);
}


// 0xFF 0xFE as int16, big-endian, is -2.
void TestReadSigned() {
	FakeRegisterBus bus;
	bus.memory[0x40] = 0xFF;
	bus.memory[0x41] = 0xFE;

	Result<int16> value = Read<Signed>(bus);
	SimCheck(value.IsOk());
	SimCheck(value.Value() == -2);
}


// A two-byte address width is sent in the transaction, and the address is the descriptor's address.
void TestTwoByteAddressWidth() {
	FakeRegisterBus bus;
	bus.memory[0x0102] = 0x7E;

	Result<uint8> value = Read<Wide>(bus);
	SimCheck(value.IsOk());
	SimCheck(value.Value() == 0x7E);
	SimCheck(bus.log[0].where.addressWidth == 2);
	SimCheck(bus.log[0].where.address == 0x0102);
}


// A bus failure is returned as the result's error, and nothing else is read.
void TestReadFailure() {
	FakeRegisterBus bus;
	bus.failWith = ResultStatus::readError;

	Result<uint8> value = Read<ChargeControl>(bus);
	SimCheck(value.IsErr());
	SimCheck(value.Error() == ResultStatus::readError);
}


// One byte is written as given.
void TestWriteOneByte() {
	FakeRegisterBus bus;
	ResultStatus status = Write<ChargeControl>(bus, 0x00);

	SimCheck(status == ResultStatus::ok);
	SimCheck(bus.log.size() == 1);
	SimCheck(!bus.log[0].isRead);
	SimCheck(bus.log[0].bytes.size() == 1);
	SimCheck(bus.log[0].bytes[0] == 0x00);
	SimCheck(bus.log[0].where.device == 0x09);
}


// 0x1234 as a big-endian uint16 goes out as 0x12 then 0x34, and the memory holds the same bytes.
void TestWriteBigEndian() {
	FakeRegisterBus bus;
	ResultStatus status = Write<Setpoint>(bus, 0x1234);

	SimCheck(status == ResultStatus::ok);
	SimCheck(bus.log[0].bytes.size() == 2);
	SimCheck(bus.log[0].bytes[0] == 0x12);
	SimCheck(bus.log[0].bytes[1] == 0x34);
	SimCheck(bus.memory[0x20] == 0x12);
	SimCheck(bus.memory[0x21] == 0x34);
}


// Modify on bit 3: one read, one write, and the other bits keep their values. 0xFF with bit 3 cleared is 0xF7.
void TestModifyClearsBit() {
	FakeRegisterBus bus;
	bus.memory[0x01] = 0xFF;

	ResultStatus status = Modify<ChargeDisable>(bus, false);
	SimCheck(status == ResultStatus::ok);
	SimCheck(bus.log.size() == 2);
	SimCheck(bus.log[0].isRead);
	SimCheck(!bus.log[1].isRead);
	SimCheck(bus.log[1].bytes.size() == 1);
	SimCheck(bus.log[1].bytes[0] == 0xF7);
	SimCheck(bus.memory[0x01] == 0xF7);
}


// Setting bit 3 on 0x00 gives 0x08.
void TestModifySetsBit() {
	FakeRegisterBus bus;
	bus.memory[0x01] = 0x00;

	ResultStatus status = Modify<ChargeDisable>(bus, true);
	SimCheck(status == ResultStatus::ok);
	SimCheck(bus.memory[0x01] == 0x08);
}


// A two-bit field at bit 5 set to 3 on 0x00 gives 0x60. On 0xFF, setting it to 0 gives 0x9F: the bits 5 and 6 clear, the rest kept.
void TestModifyMultiBitField() {
	FakeRegisterBus bus;
	bus.memory[0x01] = 0x00;
	SimCheck(Modify<ModeField>(bus, 3) == ResultStatus::ok);
	SimCheck(bus.memory[0x01] == 0x60);

	bus.memory[0x01] = 0xFF;
	SimCheck(Modify<ModeField>(bus, 0) == ResultStatus::ok);
	SimCheck(bus.memory[0x01] == 0x9F);
}


// A value that does not fit the two-bit field returns invalidParameter, and the bus is not touched.
void TestModifyValueTooWide() {
	FakeRegisterBus bus;
	bus.memory[0x01] = 0x11;

	ResultStatus status = Modify<ModeField>(bus, 4);
	SimCheck(status == ResultStatus::invalidParameter);
	SimCheck(bus.log.empty());
	SimCheck(bus.memory[0x01] == 0x11);
}


// A read failure in Modify stops it: no write follows.
void TestModifyReadFailure() {
	FakeRegisterBus bus;
	bus.failWith = ResultStatus::readError;

	ResultStatus status = Modify<ChargeDisable>(bus, true);
	SimCheck(status == ResultStatus::readError);
	SimCheck(bus.log.size() == 1);
	SimCheck(bus.log[0].isRead);
}


// A write-only register takes Write, and its value goes out as given.
void TestWriteOnlyRegister() {
	FakeRegisterBus bus;
	SimCheck(Write<Command>(bus, 0x55) == ResultStatus::ok);
	SimCheck(bus.memory[0x30] == 0x55);
}


int main() {
	TestReadOneByte();
	TestReadBigEndian();
	TestReadLittleEndian();
	TestReadSigned();
	TestTwoByteAddressWidth();
	TestReadFailure();
	TestWriteOneByte();
	TestWriteBigEndian();
	TestModifyClearsBit();
	TestModifySetsBit();
	TestModifyMultiBitField();
	TestModifyValueTooWide();
	TestModifyReadFailure();
	TestWriteOnlyRegister();

	if (simFailures != 0) {
		std::printf("TestRegisterAccess: %d check(s) failed\n", simFailures);
		return 1;
	}

	std::printf("TestRegisterAccess: all checks passed\n");
	return 0;
}
