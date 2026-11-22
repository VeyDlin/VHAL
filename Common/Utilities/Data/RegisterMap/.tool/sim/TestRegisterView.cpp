#include <Utilities/Data/RegisterMap/RegisterView.h>
#include "SimCheck.h"
#include <array>
#include <cstdio>
#include <span>


// Expected values are hand-computed from the entries below. The view owns nothing: each user variable is a global here.


uint16 setpoint = 0;
uint32 counter = 0x00010002;
std::array<uint16, 3> block{ 0, 0, 0 };
uint16 gateA = 0;
uint16 gateB = 0;
uint16 lastValue = 0;
uint32 top = 0x00000009;
uint16 readOnlyValue = 42;
bool relay = false;
std::array<bool, 3> leds{ false, false, false };


bool CheckSetpoint(uint16 value) {
	return value <= 100;
}


bool CheckGate(uint16 value) {
	return value != 0x0BAD;
}


uint16 ReadTemperature() {
	return 215;
}


uint16 ReadLast() {
	return lastValue;
}


uint16 ReadReadOnly() {
	return readOnlyValue;
}


// Fails on 7, and records every other value.
ResultStatus SetLast(uint16 value) {
	if (value == 7) {
		return ResultStatus::hardwareFailure;
	}
	lastValue = value;
	return ResultStatus::ok;
}


bool ReadButton() {
	return true;
}


using Table = RegisterView<+[] {
	return RegisterViewBuilder()
		.HoldingRegisters(0x0000, Variable(&setpoint, &CheckSetpoint))
		.HoldingRegisters(0x0010, Variable(&counter))
		.HoldingRegisters(0x0020, Variable(&block))
		.HoldingRegisters(0x0050, Variable(&gateA))
		.HoldingRegisters(0x0051, Variable(&gateB, &CheckGate))
		.HoldingRegisters(0x0052, Computed(&ReadLast, &SetLast))
		.HoldingRegisters(0x0060, Computed(&ReadReadOnly))
		.HoldingRegisters(0xFFFE, Variable(&top))
		.InputRegisters(0x0020, Computed(&ReadTemperature))
		.Coils(0x0000, Variable(&relay))
		.Coils(0x0010, Variable(&leds))
		.DiscreteInputs(0x0000, Computed(&ReadButton));
}>;


// A read of a two-register counter, most significant word first: 0x00010002 gives 1 then 2.
void TestReadTwoWordValue() {
	std::array<uint16, 2> out{ 0xEEEE, 0xEEEE };
	SimCheck(Table::ReadWords(RegisterTable::Holding, 0x0010, out) == ResultStatus::ok);
	SimCheck(out[0] == 0x0001);
	SimCheck(out[1] == 0x0002);
}


// A read of the low word alone: address 0x0011 gives 2.
void TestReadPartOfEntry() {
	std::array<uint16, 1> out{ 0 };
	SimCheck(Table::ReadWords(RegisterTable::Holding, 0x0011, out) == ResultStatus::ok);
	SimCheck(out[0] == 0x0002);
}


// Entries 0x0050, 0x0051 and 0x0052 are next to each other: one read gives all three values.
void TestReadAcrossEntries() {
	gateA = 11;
	gateB = 12;
	lastValue = 13;

	std::array<uint16, 3> out{ 0, 0, 0 };
	SimCheck(Table::ReadWords(RegisterTable::Holding, 0x0050, out) == ResultStatus::ok);
	SimCheck(out[0] == 11);
	SimCheck(out[1] == 12);
	SimCheck(out[2] == 13);
}


// Address 0x0001 has no entry. A read that reaches it fails and leaves the output alone.
void TestReadUnknownAddress() {
	std::array<uint16, 2> out{ 0xEEEE, 0xEEEE };
	SimCheck(Table::ReadWords(RegisterTable::Holding, 0x0000, out) == ResultStatus::badAddress);
	SimCheck(out[0] == 0xEEEE);
	SimCheck(out[1] == 0xEEEE);
}


// The input register at 0x0020 is a function: it returns 215.
void TestReadInputRegister() {
	std::array<uint16, 1> out{ 0 };
	SimCheck(Table::ReadWords(RegisterTable::Input, 0x0020, out) == ResultStatus::ok);
	SimCheck(out[0] == 215);
}


// An array of 16-bit integers is one register per element.
void TestWriteArrayBlock() {
	std::array<uint16, 3> values{ 7, 8, 9 };
	SimCheck(Table::WriteWords(RegisterTable::Holding, 0x0020, values) == ResultStatus::ok);
	SimCheck(block[0] == 7);
	SimCheck(block[1] == 8);
	SimCheck(block[2] == 9);

	std::array<uint16, 3> readBack{ 0, 0, 0 };
	SimCheck(Table::ReadWords(RegisterTable::Holding, 0x0020, readBack) == ResultStatus::ok);
	SimCheck(readBack[2] == 9);
}


// A write that the check accepts is stored.
void TestWriteAccepted() {
	std::array<uint16, 1> values{ 50 };
	SimCheck(Table::WriteWords(RegisterTable::Holding, 0x0000, values) == ResultStatus::ok);
	SimCheck(setpoint == 50);
}


// A value the check rejects returns invalidData and changes nothing.
void TestWriteRejected() {
	std::array<uint16, 1> values{ 200 };
	SimCheck(Table::WriteWords(RegisterTable::Holding, 0x0000, values) == ResultStatus::invalidData);
	SimCheck(setpoint == 50);
}


// Both words of the counter: 0x0005 and 0x0006 make 0x00050006.
void TestWriteTwoWordValue() {
	std::array<uint16, 2> values{ 0x0005, 0x0006 };
	SimCheck(Table::WriteWords(RegisterTable::Holding, 0x0010, values) == ResultStatus::ok);
	SimCheck(counter == 0x00050006);
}


// A write that starts inside a two-word entry does not cover it whole: badAddress, nothing written.
void TestWritePartialEntry() {
	std::array<uint16, 1> values{ 9 };
	SimCheck(Table::WriteWords(RegisterTable::Holding, 0x0011, values) == ResultStatus::badAddress);
	SimCheck(Table::WriteWords(RegisterTable::Holding, 0x0010, values) == ResultStatus::badAddress);
	SimCheck(counter == 0x00050006);
}


// A write that runs over 0xFFFF is badAddress. A write of the two words of the top entry is ok.
void TestWriteWrap() {
	std::array<uint16, 4> wrapping{ 1, 2, 3, 4 };
	SimCheck(Table::WriteWords(RegisterTable::Holding, 0xFFFE, wrapping) == ResultStatus::badAddress);
	SimCheck(top == 0x00000009);

	std::array<uint16, 2> values{ 0x0001, 0x0002 };
	SimCheck(Table::WriteWords(RegisterTable::Holding, 0xFFFE, values) == ResultStatus::ok);
	SimCheck(top == 0x00010002);
}


// A batch with a rejected value in the middle writes nothing: gateA keeps its value.
void TestWriteBatchRejectedInMiddle() {
	gateA = 1;
	gateB = 2;

	std::array<uint16, 2> values{ 5, 0x0BAD };
	SimCheck(Table::WriteWords(RegisterTable::Holding, 0x0050, values) == ResultStatus::invalidData);
	SimCheck(gateA == 1);
	SimCheck(gateB == 2);
}


// A batch with every value accepted writes all entries.
void TestWriteBatchAccepted() {
	std::array<uint16, 2> values{ 5, 6 };
	SimCheck(Table::WriteWords(RegisterTable::Holding, 0x0050, values) == ResultStatus::ok);
	SimCheck(gateA == 5);
	SimCheck(gateB == 6);
}


// A set function that fails in the middle of a batch returns error. The entry before it stays written.
void TestWriteSetFailsMidBatch() {
	std::array<uint16, 3> values{ 1, 2, 7 };
	SimCheck(Table::WriteWords(RegisterTable::Holding, 0x0050, values) == ResultStatus::error);
	SimCheck(gateA == 1);
	SimCheck(gateB == 2);
	SimCheck(lastValue == 13);
}


// A read-only entry in holding refuses a write: accessError, nothing written.
void TestWriteReadOnlyEntry() {
	std::array<uint16, 1> values{ 1 };
	SimCheck(Table::WriteWords(RegisterTable::Holding, 0x0060, values) == ResultStatus::accessError);
	SimCheck(readOnlyValue == 42);
}


// The input table is read only: a write returns accessError and the variable is not touched.
void TestWriteInputTable() {
	std::array<uint16, 1> values{ 1 };
	SimCheck(Table::WriteWords(RegisterTable::Input, 0x0020, values) == ResultStatus::accessError);
}


// Coils: a single coil, and three coils from 0x0010 set and read back.
void TestCoils() {
	std::array<bool, 1> one{ true };
	SimCheck(Table::WriteBits(RegisterTable::Coil, 0x0000, one) == ResultStatus::ok);
	SimCheck(relay);

	std::array<bool, 3> three{ true, false, true };
	SimCheck(Table::WriteBits(RegisterTable::Coil, 0x0010, three) == ResultStatus::ok);
	SimCheck(leds[0] && !leds[1] && leds[2]);

	std::array<bool, 3> readBack{ false, false, false };
	SimCheck(Table::ReadBits(RegisterTable::Coil, 0x0010, readBack) == ResultStatus::ok);
	SimCheck(readBack[0] && !readBack[1] && readBack[2]);
}


// A coil range that is not covered by an entry is badAddress.
void TestCoilUnknownAddress() {
	std::array<bool, 1> out{ true };
	SimCheck(Table::ReadBits(RegisterTable::Coil, 0x0003, out) == ResultStatus::badAddress);
}


// A discrete input is read only: a read gives its function's value, a write is accessError.
void TestDiscreteInput() {
	std::array<bool, 1> out{ false };
	SimCheck(Table::ReadBits(RegisterTable::Discrete, 0x0000, out) == ResultStatus::ok);
	SimCheck(out[0]);

	std::array<bool, 1> values{ false };
	SimCheck(Table::WriteBits(RegisterTable::Discrete, 0x0000, values) == ResultStatus::accessError);
}


bool packedRelay = true;
std::array<bool, 12> packedLeds{ true, false, true, true, false, false, false, true, true, false, true, false };


using PackedTable = RegisterView<+[] {
	return RegisterViewBuilder()
		.Coils(0x0000, Variable(&packedRelay))
		.Coils(0x0001, Variable(&packedLeds));
}>;


// Packed coil reads: the bits are packed least significant first, into bytes that are cleared first. Coil 0 is packedRelay (true) and
// coils 1 to 12 are packedLeds, so the twelve-coil entry crosses the eight-bit mark. Coils 0 to 12 give bytes 0x1B and 0x0B. Ten
// coils from 1 are packedLeds[0] to packedLeds[9], which give 0x8D (elements 0 to 7) and 0x01 (elements 8 and 9).
void TestPackedBitsAcrossEntries() {
	std::array<uint8, 2> out{ 0xFF, 0xFF };
	SimCheck(PackedTable::ReadPackedBits(RegisterTable::Coil, 0x0000, 13, out) == ResultStatus::ok);
	SimCheck(out[0] == 0x1B && out[1] == 0x0B);

	SimCheck(PackedTable::ReadPackedBits(RegisterTable::Coil, 0x0001, 10, out) == ResultStatus::ok);
	SimCheck(out[0] == 0x8D && out[1] == 0x01);
}


// The output must hold the bytes of the range, and the range must be covered by coil entries.
void TestPackedBitsRefused() {
	std::array<uint8, 1> small{};
	SimCheck(PackedTable::ReadPackedBits(RegisterTable::Coil, 0x0000, 13, small) == ResultStatus::invalidParameter);

	std::array<uint8, 2> wide{};
	SimCheck(PackedTable::ReadPackedBits(RegisterTable::Coil, 0x000D, 4, wide) == ResultStatus::badAddress);
}


int main() {
	TestPackedBitsAcrossEntries();
	TestPackedBitsRefused();
	TestReadTwoWordValue();
	TestReadPartOfEntry();
	TestReadAcrossEntries();
	TestReadUnknownAddress();
	TestReadInputRegister();
	TestWriteArrayBlock();
	TestWriteAccepted();
	TestWriteRejected();
	TestWriteTwoWordValue();
	TestWritePartialEntry();
	TestWriteWrap();
	TestWriteBatchRejectedInMiddle();
	TestWriteBatchAccepted();
	TestWriteSetFailsMidBatch();
	TestWriteReadOnlyEntry();
	TestWriteInputTable();
	TestCoils();
	TestCoilUnknownAddress();
	TestDiscreteInput();

	if (simFailures != 0) {
		std::printf("TestRegisterView: %d check(s) failed\n", simFailures);
		return 1;
	}

	std::printf("TestRegisterView: all checks passed\n");
	return 0;
}
