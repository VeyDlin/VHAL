#include "SimCheck.h"
#include "FakeGpio.h"
#include "FakeLine.h"
#include "ScriptedLink.h"
#include <Drivers/Interface/Modbus/ModbusServer.h>
#include <Drivers/Interface/Modbus/RTU/RtuFramer.h>
#include <Drivers/Interface/Modbus/RTU/RtuTransport.h>
#include <Utilities/Data/RegisterMap/RegisterView.h>
#include <Utilities/Result/ResultStatus.h>
#include <cstddef>
#include <vector>


// The server over a simulated RS-485 line: a request is put on the line, the server's Process answers it, and the reply bytes are
// read back from the line's sent bytes. The expected replies are hand-worked from the function layouts and sealed with the library CRC.


uint16 setpoint = 0;
uint16 counterValue = 0;
bool relayOn = false;
uint16 readOnlyValue = 0x0102;
uint16 pendingSecond = 0;


bool CheckSetpoint(uint16 value) {
	return value <= 100;
}


uint16 ReadOnlyValue() {
	return readOnlyValue;
}


uint16 ReadChipTemperature() {
	return 0x0042;
}


ResultStatus FailingSet(uint16 value) {
	pendingSecond = value;
	return ResultStatus::error;
}


using MainView = RegisterView<+[] {
	return RegisterViewBuilder()
		.HoldingRegisters(0x0000, Variable(&setpoint, &CheckSetpoint))
		.HoldingRegisters(0x0001, Computed(&ReadOnlyValue))
		.HoldingRegisters(0x0002, Variable(&counterValue))
		.InputRegisters(0x0010, Computed(&ReadChipTemperature))
		.Coils(0x0000, Variable(&relayOn));
}>;


using HoldingOnlyView = RegisterView<+[] {
	return RegisterViewBuilder()
		.HoldingRegisters(0x0000, Variable(&setpoint));
}>;


using FailingView = RegisterView<+[] {
	return RegisterViewBuilder()
		.HoldingRegisters(0x0000, Variable(&setpoint))
		.HoldingRegisters(0x0001, Computed(&ReadOnlyValue, &FailingSet));
}>;


using Link = RtuTransport<FakeLine, FakeGpio, +[] {
	return RtuBuilder()
		.Clock<&SimClockUs>();
}>;


using MainServer = ModbusServer<Link, RtuFramer, MainView, +[] {
	return ServerBuilder()
		.SlaveAddress(1);
}>;


using HoldingServer = ModbusServer<Link, RtuFramer, HoldingOnlyView, +[] {
	return ServerBuilder()
		.SlaveAddress(1);
}>;


using FailingServer = ModbusServer<Link, RtuFramer, FailingView, +[] {
	return ServerBuilder()
		.SlaveAddress(1);
}>;


struct Served {
	ResultStatus status;
	std::vector<uint8> reply;
};


// Puts one request on the line, one character time apart, and runs Process until it returns something other than timeout. The reply
// is the bytes the line sent during the call.
template<typename Server>
Served Serve(FakeLine& line, Server& server, const std::vector<uint8>& request) {
	std::size_t before = line.Sent().size();
	uint64 start = simNowUs + 1000;

	for (std::size_t index = 0; index < request.size(); index++) {
		line.ScheduleByte(start + index * 1145, request[index]);
	}

	ResultStatus status = ResultStatus::timeout;
	for (int step = 0; step < 200000 && status == ResultStatus::timeout; step++) {
		status = server.Process();
	}

	std::vector<uint8> reply;
	for (std::size_t index = before; index < line.Sent().size(); index++) {
		reply.push_back(line.Sent()[index].value);
	}

	return Served{status, reply};
}


void SetLine(FakeLine& line) {
	FakeLine::Parameters parameters;
	parameters.baudRate = 9600;
	line.SetParameters(parameters);
}


void ResetState() {
	setpoint = 0;
	counterValue = 0;
	relayOn = false;
	readOnlyValue = 0x0102;
	pendingSecond = 0;
	simNowUs = 1000000;
	simCriticalDepth = 0;
}


void TestReadsAnswerWithTheViewValues() {
	ResetState();
	setpoint = 0x0011;
	counterValue = 0x0203;
	FakeLine line;
	SetLine(line);
	Link link(line);
	SIM_EXPECT(link.Begin() == ResultStatus::ok);
	MainServer server(link);

	Served holding = Serve(line, server, ScriptedSeal({0x01, 0x03, 0x00, 0x00, 0x00, 0x03}));
	SIM_EXPECT(holding.status == ResultStatus::ok);
	SIM_EXPECT((holding.reply == ScriptedSeal({0x01, 0x03, 0x06, 0x00, 0x11, 0x01, 0x02, 0x02, 0x03})));

	Served input = Serve(line, server, ScriptedSeal({0x01, 0x04, 0x00, 0x10, 0x00, 0x01}));
	SIM_EXPECT((input.reply == ScriptedSeal({0x01, 0x04, 0x02, 0x00, 0x42})));

	relayOn = true;
	Served coil = Serve(line, server, ScriptedSeal({0x01, 0x01, 0x00, 0x00, 0x00, 0x01}));
	SIM_EXPECT((coil.reply == ScriptedSeal({0x01, 0x01, 0x01, 0x01})));
}


// Exceptions: 02 for a range outside the view and for a wrap past 0xFFFF, 03 for a quantity of zero or over the limit.
void TestReadExceptions() {
	ResetState();
	FakeLine line;
	SetLine(line);
	Link link(line);
	SIM_EXPECT(link.Begin() == ResultStatus::ok);
	MainServer server(link);

	Served outside = Serve(line, server, ScriptedSeal({0x01, 0x03, 0x01, 0x00, 0x00, 0x01}));
	SIM_EXPECT((outside.reply == ScriptedSeal({0x01, 0x83, 0x02})));

	Served none = Serve(line, server, ScriptedSeal({0x01, 0x03, 0x00, 0x00, 0x00, 0x00}));
	SIM_EXPECT((none.reply == ScriptedSeal({0x01, 0x83, 0x03})));

	Served tooMany = Serve(line, server, ScriptedSeal({0x01, 0x03, 0x00, 0x00, 0x00, 0x7E}));
	SIM_EXPECT((tooMany.reply == ScriptedSeal({0x01, 0x83, 0x03})));
}


// Check 9: a rejected value gives 03 and writes nothing; an echoed write changes the variable.
void TestWriteSingleRegister() {
	ResetState();
	FakeLine line;
	SetLine(line);
	Link link(line);
	SIM_EXPECT(link.Begin() == ResultStatus::ok);
	MainServer server(link);

	Served written = Serve(line, server, ScriptedSeal({0x01, 0x06, 0x00, 0x00, 0x00, 0x2A}));
	SIM_EXPECT((written.reply == ScriptedSeal({0x01, 0x06, 0x00, 0x00, 0x00, 0x2A})));
	SIM_EXPECT(setpoint == 42);

	Served rejected = Serve(line, server, ScriptedSeal({0x01, 0x06, 0x00, 0x00, 0x00, 0xC8}));
	SIM_EXPECT((rejected.reply == ScriptedSeal({0x01, 0x86, 0x03})));
	SIM_EXPECT(setpoint == 42);
}


// Check 9: a batch with a read-only entry in the middle writes nothing, and gets exception 02.
void TestBatchWithReadOnlyEntryWritesNothing() {
	ResetState();
	FakeLine line;
	SetLine(line);
	Link link(line);
	SIM_EXPECT(link.Begin() == ResultStatus::ok);
	MainServer server(link);
	setpoint = 42;

	Served batch = Serve(line, server, ScriptedSeal({0x01, 0x10, 0x00, 0x00, 0x00, 0x03, 0x06, 0x00, 0x05, 0x00, 0x06, 0x00, 0x07}));
	SIM_EXPECT((batch.reply == ScriptedSeal({0x01, 0x90, 0x02})));
	SIM_EXPECT(setpoint == 42);
	SIM_EXPECT(counterValue == 0);
}


// Check 9: a batch that crosses 0xFFFF gives 02 and changes nothing.
void TestBatchPastTheEndIsRefused() {
	ResetState();
	FakeLine line;
	SetLine(line);
	Link link(line);
	SIM_EXPECT(link.Begin() == ResultStatus::ok);
	MainServer server(link);
	counterValue = 7;

	Served wrap = Serve(line, server, ScriptedSeal({0x01, 0x10, 0xFF, 0xFE, 0x00, 0x04, 0x08, 0x00, 0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x04}));
	SIM_EXPECT((wrap.reply == ScriptedSeal({0x01, 0x90, 0x02})));
	SIM_EXPECT(counterValue == 7);
	SIM_EXPECT(setpoint == 0);
}


// A batch that fits writes every entry, and its reply echoes the address and the quantity.
void TestBatchWritesTheWholeRange() {
	ResetState();
	FakeLine line;
	SetLine(line);
	Link link(line);
	SIM_EXPECT(link.Begin() == ResultStatus::ok);
	MainServer server(link);

	Served batch = Serve(line, server, ScriptedSeal({0x01, 0x10, 0x00, 0x02, 0x00, 0x01, 0x02, 0x00, 0x09}));
	SIM_EXPECT((batch.reply == ScriptedSeal({0x01, 0x10, 0x00, 0x02, 0x00, 0x01})));
	SIM_EXPECT(counterValue == 9);
}


void TestWriteCoilsAndSingleCoil() {
	ResetState();
	FakeLine line;
	SetLine(line);
	Link link(line);
	SIM_EXPECT(link.Begin() == ResultStatus::ok);
	MainServer server(link);

	Served coils = Serve(line, server, ScriptedSeal({0x01, 0x0F, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01}));
	SIM_EXPECT((coils.reply == ScriptedSeal({0x01, 0x0F, 0x00, 0x00, 0x00, 0x01})));
	SIM_EXPECT(relayOn);

	Served single = Serve(line, server, ScriptedSeal({0x01, 0x05, 0x00, 0x00, 0x00, 0x00}));
	SIM_EXPECT((single.reply == ScriptedSeal({0x01, 0x05, 0x00, 0x00, 0x00, 0x00})));
	SIM_EXPECT(!relayOn);

	Served badValue = Serve(line, server, ScriptedSeal({0x01, 0x05, 0x00, 0x00, 0x12, 0x34}));
	SIM_EXPECT((badValue.reply == ScriptedSeal({0x01, 0x85, 0x03})));
}


// A function code the view does not enable gets exception 01, and so does a table the view does not have.
void TestDisabledFunctionCodesGetException01() {
	ResetState();
	FakeLine line;
	SetLine(line);
	Link link(line);
	SIM_EXPECT(link.Begin() == ResultStatus::ok);
	MainServer server(link);

	Served unknown = Serve(line, server, ScriptedSeal({0x01, 0x07}));
	SIM_EXPECT((unknown.reply == ScriptedSeal({0x01, 0x87, 0x01})));

	Served discrete = Serve(line, server, ScriptedSeal({0x01, 0x02, 0x00, 0x00, 0x00, 0x01}));
	SIM_EXPECT((discrete.reply == ScriptedSeal({0x01, 0x82, 0x01})));
}


void TestHoldingOnlyServerDoesNotEnableCoils() {
	ResetState();
	FakeLine line;
	SetLine(line);
	Link link(line);
	SIM_EXPECT(link.Begin() == ResultStatus::ok);
	HoldingServer server(link);

	Served coils = Serve(line, server, ScriptedSeal({0x01, 0x01, 0x00, 0x00, 0x00, 0x01}));
	SIM_EXPECT((coils.reply == ScriptedSeal({0x01, 0x81, 0x01})));

	Served read = Serve(line, server, ScriptedSeal({0x01, 0x03, 0x00, 0x00, 0x00, 0x01}));
	SIM_EXPECT((read.reply == ScriptedSeal({0x01, 0x03, 0x02, 0x00, 0x00})));
}


// A bad CRC and a frame for another slave get no reply.
void TestBadCrcAndOtherSlaveGetNoReply() {
	ResetState();
	FakeLine line;
	SetLine(line);
	Link link(line);
	SIM_EXPECT(link.Begin() == ResultStatus::ok);
	MainServer server(link);

	std::vector<uint8> bad = ScriptedSeal({0x01, 0x03, 0x00, 0x00, 0x00, 0x01});
	bad[bad.size() - 1] = static_cast<uint8>(bad[bad.size() - 1] ^ 0xFF);
	Served badCrc = Serve(line, server, bad);
	SIM_EXPECT(badCrc.status == ResultStatus::crcError);
	SIM_EXPECT(badCrc.reply.empty());

	Served other = Serve(line, server, ScriptedSeal({0x02, 0x03, 0x00, 0x00, 0x00, 0x01}));
	SIM_EXPECT(other.status == ResultStatus::ok);
	SIM_EXPECT(other.reply.empty());
}


// A broadcast write runs and gets no reply. A broadcast read is ignored.
void TestBroadcastWritesRunWithoutReply() {
	ResetState();
	FakeLine line;
	SetLine(line);
	Link link(line);
	SIM_EXPECT(link.Begin() == ResultStatus::ok);
	MainServer server(link);

	Served write = Serve(line, server, ScriptedSeal({0x00, 0x06, 0x00, 0x00, 0x00, 0x07}));
	SIM_EXPECT(write.status == ResultStatus::ok);
	SIM_EXPECT(write.reply.empty());
	SIM_EXPECT(setpoint == 7);

	Served read = Serve(line, server, ScriptedSeal({0x00, 0x03, 0x00, 0x00, 0x00, 0x01}));
	SIM_EXPECT(read.status == ResultStatus::ok);
	SIM_EXPECT(read.reply.empty());
}


// Check 9: a set function that fails in the middle of a batch gives 04. The entries before it stay written, as the docs say.
void TestSetFailureGivesException04() {
	ResetState();
	FakeLine line;
	SetLine(line);
	Link link(line);
	SIM_EXPECT(link.Begin() == ResultStatus::ok);
	FailingServer server(link);

	Served batch = Serve(line, server, ScriptedSeal({0x01, 0x10, 0x00, 0x00, 0x00, 0x02, 0x04, 0x00, 0x21, 0x00, 0x22}));
	SIM_EXPECT((batch.reply == ScriptedSeal({0x01, 0x90, 0x04})));
	SIM_EXPECT(setpoint == 0x21);
	SIM_EXPECT(pendingSecond == 0x22);
}


uint64 smallWide = 0;
uint32 middleWide = 0;
int wideReads = 0;


// The value changes on each read, so two reads of one entry would give halves from two snapshots.
uint64 ReadWide() {
	wideReads++;
	return 0x1000200030004000ULL + static_cast<uint64>(wideReads);
}


using WideView = RegisterView<+[] {
	return RegisterViewBuilder()
		.HoldingRegisters(0x0000, Variable(&smallWide))
		.HoldingRegisters(0x0004, Variable(&middleWide))
		.HoldingRegisters(0x0006, Computed(&ReadWide));
}>;


using WideServer = ModbusServer<Link, RtuFramer, WideView, +[] {
	return ServerBuilder()
		.SlaveAddress(1);
}>;


// A 64-bit entry at words 6 to 9 crosses the eight-word chunk of a read of ten words. The entry is read once, so the reply carries one
// snapshot: the last word is 0x4001 for the one read.
void TestWideEntryIsReadOnce() {
	ResetState();
	smallWide = 0;
	middleWide = 0;
	wideReads = 0;
	FakeLine line;
	SetLine(line);
	Link link(line);
	SIM_EXPECT(link.Begin() == ResultStatus::ok);
	WideServer server(link);

	Served reply = Serve(line, server, ScriptedSeal({0x01, 0x03, 0x00, 0x00, 0x00, 0x0A}));
	SIM_EXPECT(wideReads == 1);
	SIM_EXPECT((reply.reply == ScriptedSeal({0x01, 0x03, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x10, 0x00, 0x20, 0x00, 0x30, 0x00, 0x40, 0x01})));
}


int countedCoilReads = 0;
int countedTwelveReads = 0;


bool CountedCoil() {
	countedCoilReads++;
	return true;
}


std::array<bool, 12> CountedTwelve() {
	countedTwelveReads++;
	return {true, false, true, true, false, false, false, true, true, false, true, false};
}


using CoilCountView = RegisterView<+[] {
	return RegisterViewBuilder()
		.Coils(0x0000, Computed(&CountedCoil))
		.Coils(0x0001, Computed(&CountedTwelve));
}>;


using CoilCountServer = ModbusServer<Link, RtuFramer, CoilCountView, +[] {
	return ServerBuilder()
		.SlaveAddress(1);
}>;


// A read of 13 coils from address 0 crosses the eight-bit mark of the twelve-bit entry. Each entry is read once per request, so each
// source runs once. The bits are packed least significant first: positions 0 to 7 are 1,1,0,1,1,0,0,0 (0x1B), and positions 8 to 12
// are 1,1,0,1,0 (0x0B). Position 0 is the computed coil, and positions 1 to 12 are the twelve elements in order.
void TestCoilReadsReadEachEntryOnce() {
	ResetState();
	countedCoilReads = 0;
	countedTwelveReads = 0;
	FakeLine line;
	SetLine(line);
	Link link(line);
	SIM_EXPECT(link.Begin() == ResultStatus::ok);
	CoilCountServer server(link);

	Served reply = Serve(line, server, ScriptedSeal({0x01, 0x01, 0x00, 0x00, 0x00, 0x0D}));
	SIM_EXPECT(countedCoilReads == 1);
	SIM_EXPECT(countedTwelveReads == 1);
	SIM_EXPECT((reply.reply == ScriptedSeal({0x01, 0x01, 0x02, 0x1B, 0x0B})));
}


std::array<bool, 8> coilBank{};
std::array<uint16, 3> holdingBank{ 1, 2, 3 };


using BlockView = RegisterView<+[] {
	return RegisterViewBuilder()
		.Coils(0x0000, Variable(&coilBank))
		.HoldingRegisters(0x0000, Variable(&holdingBank));
}>;


using BlockServer = ModbusServer<Link, RtuFramer, BlockView, +[] {
	return ServerBuilder()
		.SlaveAddress(1);
}>;


// FC05 to coil 3 of an eight-coil block: the reply echoes the request (value FF00 is on), and only coil 3 is set.
void TestWriteSingleCoilOfABlock() {
	ResetState();
	coilBank = {};
	FakeLine line;
	SetLine(line);
	Link link(line);
	SIM_EXPECT(link.Begin() == ResultStatus::ok);
	BlockServer server(link);

	Served single = Serve(line, server, ScriptedSeal({0x01, 0x05, 0x00, 0x03, 0xFF, 0x00}));
	SIM_EXPECT(single.status == ResultStatus::ok);
	SIM_EXPECT((single.reply == ScriptedSeal({0x01, 0x05, 0x00, 0x03, 0xFF, 0x00})));
	SIM_EXPECT(!coilBank[0] && !coilBank[1] && !coilBank[2]);
	SIM_EXPECT(coilBank[3]);
	SIM_EXPECT(!coilBank[4] && !coilBank[5] && !coilBank[6] && !coilBank[7]);
}


// FC06 to element 1 of a three-register block: the reply echoes the request, and only element 1 changes.
void TestWriteSingleRegisterOfABlock() {
	ResetState();
	holdingBank = { 1, 2, 3 };
	FakeLine line;
	SetLine(line);
	Link link(line);
	SIM_EXPECT(link.Begin() == ResultStatus::ok);
	BlockServer server(link);

	Served single = Serve(line, server, ScriptedSeal({0x01, 0x06, 0x00, 0x01, 0x12, 0x34}));
	SIM_EXPECT(single.status == ResultStatus::ok);
	SIM_EXPECT((single.reply == ScriptedSeal({0x01, 0x06, 0x00, 0x01, 0x12, 0x34})));
	SIM_EXPECT(holdingBank[0] == 1);
	SIM_EXPECT(holdingBank[1] == 0x1234);
	SIM_EXPECT(holdingBank[2] == 3);
}


// FC15 to coil 3 of an eight-coil block: the reply echoes the start and the count, and only coil 3 is set.
void TestWriteMultipleCoilsOfABlock() {
	ResetState();
	coilBank = {};
	FakeLine line;
	SetLine(line);
	Link link(line);
	SIM_EXPECT(link.Begin() == ResultStatus::ok);
	BlockServer server(link);

	Served multiple = Serve(line, server, ScriptedSeal({0x01, 0x0F, 0x00, 0x03, 0x00, 0x01, 0x01, 0x01}));
	SIM_EXPECT(multiple.status == ResultStatus::ok);
	SIM_EXPECT((multiple.reply == ScriptedSeal({0x01, 0x0F, 0x00, 0x03, 0x00, 0x01})));
	SIM_EXPECT(!coilBank[0] && !coilBank[1] && !coilBank[2]);
	SIM_EXPECT(coilBank[3]);
	SIM_EXPECT(!coilBank[4] && !coilBank[5] && !coilBank[6] && !coilBank[7]);
}


// FC16 to element 1 of a three-register block: the reply echoes the start and the count, and only element 1 changes.
void TestWriteMultipleRegistersOfABlock() {
	ResetState();
	holdingBank = { 1, 2, 3 };
	FakeLine line;
	SetLine(line);
	Link link(line);
	SIM_EXPECT(link.Begin() == ResultStatus::ok);
	BlockServer server(link);

	Served multiple = Serve(line, server, ScriptedSeal({0x01, 0x10, 0x00, 0x01, 0x00, 0x01, 0x02, 0x12, 0x34}));
	SIM_EXPECT(multiple.status == ResultStatus::ok);
	SIM_EXPECT((multiple.reply == ScriptedSeal({0x01, 0x10, 0x00, 0x01, 0x00, 0x01})));
	SIM_EXPECT(holdingBank[0] == 1);
	SIM_EXPECT(holdingBank[1] == 0x1234);
	SIM_EXPECT(holdingBank[2] == 3);
}


int main() {
	TestWriteSingleCoilOfABlock();
	TestWriteSingleRegisterOfABlock();
	TestWriteMultipleCoilsOfABlock();
	TestWriteMultipleRegistersOfABlock();
	TestCoilReadsReadEachEntryOnce();
	TestWideEntryIsReadOnce();
	TestReadsAnswerWithTheViewValues();
	TestReadExceptions();
	TestWriteSingleRegister();
	TestBatchWithReadOnlyEntryWritesNothing();
	TestBatchPastTheEndIsRefused();
	TestBatchWritesTheWholeRange();
	TestWriteCoilsAndSingleCoil();
	TestDisabledFunctionCodesGetException01();
	TestHoldingOnlyServerDoesNotEnableCoils();
	TestBadCrcAndOtherSlaveGetNoReply();
	TestBroadcastWritesRunWithoutReply();
	TestSetFailureGivesException04();
	return SimFinish("TestModbusServer");
}
