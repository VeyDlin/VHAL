#include "SimCheck.h"
#include <Drivers/Interface/Modbus/Pdu/ReadCoils.h>
#include <Drivers/Interface/Modbus/Pdu/ReadDiscreteInputs.h>
#include <Drivers/Interface/Modbus/Pdu/ReadHoldingRegisters.h>
#include <Drivers/Interface/Modbus/Pdu/ReadInputRegisters.h>
#include <Drivers/Interface/Modbus/Pdu/WriteMultipleCoils.h>
#include <Drivers/Interface/Modbus/Pdu/WriteMultipleRegisters.h>
#include <Drivers/Interface/Modbus/Pdu/WriteSingleCoil.h>
#include <Drivers/Interface/Modbus/Pdu/WriteSingleRegister.h>
#include <array>
#include <cstddef>
#include <span>


// The request and reply bytes below are the examples of the Modbus Application Protocol specification (V1.1b3, section 6), and
// hand-computed frames for the cases the specification does not show. Expected values never come from the code under test.


// Builds the read holding registers request at compile time: the builder is constexpr, so the bytes are checked as a constant.
consteval bool HoldingRequestBuildsAtCompileTime() {
	std::array<uint8, 5> pdu{};
	return ReadHoldingRegistersPdu::BuildRequest(pdu, 0x006B, 3) == ResultStatus::ok && pdu[0] == 0x03 && pdu[2] == 0x6B && pdu[4] == 0x03;
}


static_assert(HoldingRequestBuildsAtCompileTime());


void TestReadHoldingRegisters() {
	std::array<uint8, 5> request{};
	SIM_EXPECT(ReadHoldingRegistersPdu::BuildRequest(request, 0x006B, 3) == ResultStatus::ok);
	// Spec example: 03 006B 0003
	SIM_EXPECT(request[0] == 0x03 && request[1] == 0x00 && request[2] == 0x6B && request[3] == 0x00 && request[4] == 0x03);

	std::array<uint8, 5> bad{};
	SIM_EXPECT(ReadHoldingRegistersPdu::BuildRequest(bad, 0x0000, 0) == ResultStatus::invalidParameter);
	SIM_EXPECT(ReadHoldingRegistersPdu::BuildRequest(bad, 0x0000, 126) == ResultStatus::invalidParameter);
	SIM_EXPECT(ReadHoldingRegistersPdu::BuildRequest(bad, 0x0000, 125) == ResultStatus::ok);

	// Spec example reply for three registers: 03 06 022B 0000 0064
	std::array<uint8, 8> reply = {0x03, 0x06, 0x02, 0x2B, 0x00, 0x00, 0x00, 0x64};
	ModbusResult checked = ReadHoldingRegistersPdu::CheckReply(request, reply);
	SIM_EXPECT(checked.status == ResultStatus::ok);

	std::array<uint16, 3> values{};
	ReadHoldingRegistersPdu::ReadValues(reply, values);
	SIM_EXPECT(values[0] == 0x022B && values[1] == 0x0000 && values[2] == 0x0064);

	// Ten registers asked, one register sent: the byte count is 2, not 20.
	std::array<uint8, 5> ten{};
	ReadHoldingRegistersPdu::BuildRequest(ten, 0x0000, 10);
	std::array<uint8, 4> shortReply = {0x03, 0x02, 0x00, 0x01};
	SIM_EXPECT(ReadHoldingRegistersPdu::CheckReply(ten, shortReply).status == ResultStatus::invalidData);

	// Exception 02 is two bytes: 83 02.
	std::array<uint8, 2> exception = {0x83, 0x02};
	ModbusResult failed = ReadHoldingRegistersPdu::CheckReply(request, exception);
	SIM_EXPECT(failed.status == ResultStatus::error && failed.exception == 0x02);

	// An exception of the wrong length is invalid.
	std::array<uint8, 3> longException = {0x83, 0x02, 0x00};
	SIM_EXPECT(ReadHoldingRegistersPdu::CheckReply(request, longException).status == ResultStatus::invalidData);

	// Another function code in the reply is invalid.
	std::array<uint8, 6> otherCode = {0x04, 0x04, 0x00, 0x01, 0x00, 0x02};
	SIM_EXPECT(ReadHoldingRegistersPdu::CheckReply(request, otherCode).status == ResultStatus::invalidData);
}


void TestReadInputRegisters() {
	std::array<uint8, 5> request{};
	ReadInputRegistersPdu::BuildRequest(request, 0x0010, 1);
	SIM_EXPECT(request[0] == 0x04 && request[2] == 0x10 && request[4] == 0x01);

	std::array<uint8, 4> reply = {0x04, 0x02, 0x12, 0x34};
	SIM_EXPECT(ReadInputRegistersPdu::CheckReply(request, reply).status == ResultStatus::ok);

	std::array<uint16, 1> value{};
	ReadInputRegistersPdu::ReadValues(reply, value);
	SIM_EXPECT(value[0] == 0x1234);

	SIM_EXPECT(ReadInputRegistersPdu::BuildRequest(request, 0x0000, 125) == ResultStatus::ok);
	SIM_EXPECT(ReadInputRegistersPdu::BuildRequest(request, 0x0000, 126) == ResultStatus::invalidParameter);
}


void TestReadCoils() {
	// Spec example: 01 0013 0013 (19 coils from 20)
	std::array<uint8, 5> request{};
	SIM_EXPECT(ReadCoilsPdu::BuildRequest(request, 0x0013, 19) == ResultStatus::ok);
	SIM_EXPECT(request[0] == 0x01 && request[1] == 0x00 && request[2] == 0x13 && request[3] == 0x00 && request[4] == 0x13);

	// Spec example reply: 01 03 CD 6B 05. Bits of CD: 1 0 1 1 0 0 1 1; of 6B: 1 1 0 1 0 1 1 0; of 05: 1 0 1.
	std::array<uint8, 5> reply = {0x01, 0x03, 0xCD, 0x6B, 0x05};
	SIM_EXPECT(ReadCoilsPdu::CheckReply(request, reply).status == ResultStatus::ok);

	std::array<bool, 19> bits{};
	ReadCoilsPdu::ReadBits(reply, bits);
	const std::array<bool, 19> expected = {
		true, false, true, true, false, false, true, true,
		true, true, false, true, false, true, true, false,
		true, false, true,
	};
	bool allMatch = true;
	for (std::size_t index = 0; index < bits.size(); index++) {
		allMatch = allMatch && bits[index] == expected[index];
	}
	SIM_EXPECT(allMatch);

	// The byte count must be the bit bytes of 19 coils, which is 3.
	std::array<uint8, 4> wrongCount = {0x01, 0x02, 0xCD, 0x6B};
	SIM_EXPECT(ReadCoilsPdu::CheckReply(request, wrongCount).status == ResultStatus::invalidData);

	SIM_EXPECT(ReadCoilsPdu::BuildRequest(request, 0x0000, 2000) == ResultStatus::ok);
	SIM_EXPECT(ReadCoilsPdu::BuildRequest(request, 0x0000, 2001) == ResultStatus::invalidParameter);
	SIM_EXPECT(ReadCoilsPdu::BuildRequest(request, 0x0000, 0) == ResultStatus::invalidParameter);
}


void TestReadDiscreteInputs() {
	std::array<uint8, 5> request{};
	ReadDiscreteInputsPdu::BuildRequest(request, 0x00C4, 22);
	// 22 inputs take three bytes. Reply 02 03 AC DB 35: bits of AC: 0 0 1 1 0 1 0 1.
	std::array<uint8, 5> reply = {0x02, 0x03, 0xAC, 0xDB, 0x35};
	SIM_EXPECT(ReadDiscreteInputsPdu::CheckReply(request, reply).status == ResultStatus::ok);

	std::array<bool, 22> bits{};
	ReadDiscreteInputsPdu::ReadBits(reply, bits);
	SIM_EXPECT(!bits[0] && !bits[1] && bits[2] && bits[3] && !bits[4] && bits[5] && !bits[6] && bits[7]);

	std::array<uint8, 2> exception = {0x82, 0x02};
	ModbusResult failed = ReadDiscreteInputsPdu::CheckReply(request, exception);
	SIM_EXPECT(failed.status == ResultStatus::error && failed.exception == 0x02);
}


void TestWriteSingleCoil() {
	// Spec example: 05 00AC FF00 writes ON to coil 172.
	std::array<uint8, 5> request{};
	SIM_EXPECT(WriteSingleCoilPdu::BuildRequest(request, 0x00AC, true) == ResultStatus::ok);
	SIM_EXPECT(request[0] == 0x05 && request[1] == 0x00 && request[2] == 0xAC && request[3] == 0xFF && request[4] == 0x00);

	SIM_EXPECT(WriteSingleCoilPdu::BuildRequest(request, 0x00AC, false) == ResultStatus::ok);
	SIM_EXPECT(request[3] == 0x00 && request[4] == 0x00);

	SIM_EXPECT(WriteSingleCoilPdu::CheckReply(request, request).status == ResultStatus::ok);
}


void TestWriteSingleRegister() {
	// Spec example: 06 0001 0003
	std::array<uint8, 5> request{};
	SIM_EXPECT(WriteSingleRegisterPdu::BuildRequest(request, 0x0001, 0x0003) == ResultStatus::ok);
	SIM_EXPECT(request[0] == 0x06 && request[1] == 0x00 && request[2] == 0x01 && request[3] == 0x00 && request[4] == 0x03);
	SIM_EXPECT(WriteSingleRegisterPdu::CheckReply(request, request).status == ResultStatus::ok);

	// The request is to address 50. An echo of address 51 is invalid.
	std::array<uint8, 5> toFifty{};
	WriteSingleRegisterPdu::BuildRequest(toFifty, 50, 0x1234);
	std::array<uint8, 5> echoFiftyOne = {0x06, 0x00, 0x33, 0x12, 0x34};
	SIM_EXPECT(WriteSingleRegisterPdu::CheckReply(toFifty, echoFiftyOne).status == ResultStatus::invalidData);

	// Exception 02 for function 06: 86 02 (06 with bit 0x80 set).
	std::array<uint8, 2> exception = {0x86, 0x02};
	ModbusResult failed = WriteSingleRegisterPdu::CheckReply(toFifty, exception);
	SIM_EXPECT(failed.status == ResultStatus::error && failed.exception == 0x02);
}


void TestWriteMultipleCoils() {
	// Spec example: 0F 0013 000A, two bytes CD 01, which is ten coils from 20: 1 0 1 1 0 0 1 1 | 1 0.
	std::array<bool, 10> values = {true, false, true, true, false, false, true, true, true, false};
	std::array<uint8, 8> request{};
	SIM_EXPECT(WriteMultipleCoilsPdu::BuildRequest(request, 0x0013, values) == ResultStatus::ok);
	const std::array<uint8, 8> expected = {0x0F, 0x00, 0x13, 0x00, 0x0A, 0x02, 0xCD, 0x01};
	SIM_EXPECT(request == expected);
	SIM_EXPECT(WriteMultipleCoilsPdu::RequestSize(values.size()) == 8);

	// Spec example reply: 0F 0013 000A
	std::array<uint8, 5> reply = {0x0F, 0x00, 0x13, 0x00, 0x0A};
	SIM_EXPECT(WriteMultipleCoilsPdu::CheckReply(request, reply).status == ResultStatus::ok);

	// The reply echoes the quantity: a quantity of 9 is invalid.
	std::array<uint8, 5> wrongQuantity = {0x0F, 0x00, 0x13, 0x00, 0x09};
	SIM_EXPECT(WriteMultipleCoilsPdu::CheckReply(request, wrongQuantity).status == ResultStatus::invalidData);

	// Exception 03 for a write of coils: 8F 03.
	std::array<uint8, 2> exception = {0x8F, 0x03};
	ModbusResult failed = WriteMultipleCoilsPdu::CheckReply(request, exception);
	SIM_EXPECT(failed.status == ResultStatus::error && failed.exception == 0x03);

	// A buffer that is too short is refused.
	std::array<uint8, 7> small{};
	SIM_EXPECT(WriteMultipleCoilsPdu::BuildRequest(small, 0x0013, values) == ResultStatus::invalidParameter);

	std::array<bool, 0> empty{};
	SIM_EXPECT(WriteMultipleCoilsPdu::BuildRequest(request, 0x0000, empty) == ResultStatus::invalidParameter);
}


void TestWriteMultipleRegisters() {
	// Spec example: 10 0001 0002 with values 000A and 0102
	std::array<uint16, 2> values = {0x000A, 0x0102};
	std::array<uint8, 10> request{};
	SIM_EXPECT(WriteMultipleRegistersPdu::BuildRequest(request, 0x0001, values) == ResultStatus::ok);
	const std::array<uint8, 10> expected = {0x10, 0x00, 0x01, 0x00, 0x02, 0x04, 0x00, 0x0A, 0x01, 0x02};
	SIM_EXPECT(request == expected);

	// Spec example reply: 10 0001 0002
	std::array<uint8, 5> reply = {0x10, 0x00, 0x01, 0x00, 0x02};
	SIM_EXPECT(WriteMultipleRegistersPdu::CheckReply(request, reply).status == ResultStatus::ok);

	// Quantity limits: 123 is the most, 124 is refused.
	std::array<uint16, 123> many{};
	std::array<uint8, 260> big{};
	SIM_EXPECT(WriteMultipleRegistersPdu::BuildRequest(big, 0x0000, many) == ResultStatus::ok);
	SIM_EXPECT(WriteMultipleRegistersPdu::RequestSize(many.size()) == 252);

	std::array<uint16, 124> tooMany{};
	SIM_EXPECT(WriteMultipleRegistersPdu::BuildRequest(big, 0x0000, tooMany) == ResultStatus::invalidParameter);

	// A pdu too short for the values is refused.
	std::array<uint8, 9> small{};
	SIM_EXPECT(WriteMultipleRegistersPdu::BuildRequest(small, 0x0001, values) == ResultStatus::invalidParameter);
}


void TestLimits() {
	SIM_EXPECT(ReadCoilsPdu::MaxQuantity == 2000);
	SIM_EXPECT(ReadDiscreteInputsPdu::MaxQuantity == 2000);
	SIM_EXPECT(ReadHoldingRegistersPdu::MaxQuantity == 125);
	SIM_EXPECT(ReadInputRegistersPdu::MaxQuantity == 125);
	SIM_EXPECT(WriteMultipleCoilsPdu::MaxQuantity == 1968);
	SIM_EXPECT(WriteMultipleRegistersPdu::MaxQuantity == 123);

	SIM_EXPECT(ReadHoldingRegistersPdu::Code == 0x03);
	SIM_EXPECT(ReadInputRegistersPdu::Code == 0x04);
	SIM_EXPECT(WriteSingleCoilPdu::Code == 0x05);
	SIM_EXPECT(WriteSingleRegisterPdu::Code == 0x06);
	SIM_EXPECT(WriteMultipleCoilsPdu::Code == 0x0F);
	SIM_EXPECT(WriteMultipleRegistersPdu::Code == 0x10);
}


void TestGenericReplyChecks() {
	// An empty reply is invalid, and so is one that has only a function code and no byte count for a read.
	std::array<uint8, 5> request{};
	ReadHoldingRegistersPdu::BuildRequest(request, 0x0000, 1);
	std::span<const uint8> none;
	SIM_EXPECT(ReadHoldingRegistersPdu::CheckReply(request, none).status == ResultStatus::invalidData);

	std::array<uint8, 1> codeOnly = {0x03};
	SIM_EXPECT(ReadHoldingRegistersPdu::CheckReply(request, codeOnly).status == ResultStatus::invalidData);

	// A request whose quantity is 0 is refused before the reply is looked at.
	std::array<uint8, 5> zero = {0x03, 0x00, 0x00, 0x00, 0x00};
	std::array<uint8, 2> anything = {0x03, 0x00};
	SIM_EXPECT(ReadHoldingRegistersPdu::CheckReply(zero, anything).status == ResultStatus::invalidParameter);
}


int main() {
	TestReadHoldingRegisters();
	TestReadInputRegisters();
	TestReadCoils();
	TestReadDiscreteInputs();
	TestWriteSingleCoil();
	TestWriteSingleRegister();
	TestWriteMultipleCoils();
	TestWriteMultipleRegisters();
	TestLimits();
	TestGenericReplyChecks();
	return SimFinish("TestPdu");
}
