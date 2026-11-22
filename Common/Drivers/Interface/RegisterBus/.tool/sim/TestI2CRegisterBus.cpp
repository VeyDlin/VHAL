#include <Drivers/Interface/RegisterBus/I2CRegisterBus.h>
#include <Utilities/Data/RegisterMap/BitField.h>
#include <Utilities/Data/RegisterMap/RegisterAccess.h>
#include <Utilities/Result/Result.h>
#include "FakeBus.h"
#include "SimCheck.h"
#include <cstdio>


// Expected bytes are hand-computed from the layouts below. The fake adapter logs each ReadArray and WriteArray call.


constexpr auto Measured = RegisterDescriptor<uint16>(0x68, 0x3B).ReadOnly();
constexpr auto Setpoint = RegisterDescriptor<uint16>(0x68, 0x20);
constexpr auto Wide = RegisterDescriptor<uint8>(0x50, 0x0102).AddressWidth(2);
constexpr auto Narrow = RegisterDescriptor<uint8>(0x50, 0x0102);
constexpr auto Control = RegisterDescriptor<uint8>(0x68, 0x09);
constexpr auto ChargeDisable = BitField<1>(Control, 3);


// A two-byte value at 0x3B: the device sends the address as one byte, and the value comes back big-endian.
void TestReadOverI2c() {
	FakeI2cAdapter adapter;
	adapter.memory[0x3B] = 0x12;
	adapter.memory[0x3C] = 0x34;
	I2CRegisterBus<FakeI2cAdapter> bus(adapter);

	Result<uint16> value = Read<Measured>(bus);
	SimCheck(value.IsOk());
	SimCheck(value.Value() == 0x1234);
	SimCheck(adapter.log.size() == 1);
	SimCheck(adapter.log[0].isRead);
	SimCheck(adapter.log[0].device == 0x68);
	SimCheck(adapter.log[0].address == 0x3B);
	SimCheck(adapter.log[0].addressSize == 1);
	SimCheck(adapter.log[0].bytes.size() == 2);
	SimCheck(adapter.log[0].bytes[0] == 0x12);
	SimCheck(adapter.log[0].bytes[1] == 0x34);
}


// 0x1234 goes to 0x20 as 12 34, one address byte.
void TestWriteOverI2c() {
	FakeI2cAdapter adapter;
	I2CRegisterBus<FakeI2cAdapter> bus(adapter);

	SimCheck(Write<Setpoint>(bus, 0x1234) == ResultStatus::ok);
	SimCheck(adapter.log.size() == 1);
	SimCheck(!adapter.log[0].isRead);
	SimCheck(adapter.log[0].address == 0x20);
	SimCheck(adapter.log[0].bytes.size() == 2);
	SimCheck(adapter.log[0].bytes[0] == 0x12);
	SimCheck(adapter.log[0].bytes[1] == 0x34);
	SimCheck(adapter.memory[0x20] == 0x12);
}


// A two-byte register address goes out as two address bytes: the adapter gets addressSize 2 and address 0x0102.
void TestTwoByteAddressOverI2c() {
	FakeI2cAdapter adapter;
	I2CRegisterBus<FakeI2cAdapter> bus(adapter);

	SimCheck(Write<Wide>(bus, 0x7E) == ResultStatus::ok);
	SimCheck(adapter.log[0].addressSize == 2);
	SimCheck(adapter.log[0].address == 0x0102);
	SimCheck(adapter.memory[0x0102] == 0x7E);
}


// A one-byte address width with an address above 0xFF is invalidParameter, and nothing is sent.
void TestWideAddressWithOneByteWidth() {
	FakeI2cAdapter adapter;
	I2CRegisterBus<FakeI2cAdapter> bus(adapter);

	Result<uint8> value = Read<Narrow>(bus);
	SimCheck(value.IsErr());
	SimCheck(value.Error() == ResultStatus::invalidParameter);
	SimCheck(adapter.log.empty());
}


// Modify over I2C: one read of 0x09, then one write with bit 3 cleared. 0xFF becomes 0xF7.
void TestModifyOverI2c() {
	FakeI2cAdapter adapter;
	adapter.memory[0x09] = 0xFF;
	I2CRegisterBus<FakeI2cAdapter> bus(adapter);

	SimCheck(Modify<ChargeDisable>(bus, false) == ResultStatus::ok);
	SimCheck(adapter.log.size() == 2);
	SimCheck(adapter.log[0].isRead);
	SimCheck(!adapter.log[1].isRead);
	SimCheck(adapter.log[1].bytes[0] == 0xF7);
	SimCheck(adapter.memory[0x09] == 0xF7);
}


// A bus error is returned as the read's error.
void TestI2cBusError() {
	FakeI2cAdapter adapter;
	adapter.failWith = ResultStatus::busError;
	I2CRegisterBus<FakeI2cAdapter> bus(adapter);

	Result<uint16> value = Read<Measured>(bus);
	SimCheck(value.IsErr());
	SimCheck(value.Error() == ResultStatus::busError);
}


int main() {
	TestReadOverI2c();
	TestWriteOverI2c();
	TestTwoByteAddressOverI2c();
	TestWideAddressWithOneByteWidth();
	TestModifyOverI2c();
	TestI2cBusError();

	if (simFailures != 0) {
		std::printf("TestI2CRegisterBus: %d check(s) failed\n", simFailures);
		return 1;
	}

	std::printf("TestI2CRegisterBus: all checks passed\n");
	return 0;
}
