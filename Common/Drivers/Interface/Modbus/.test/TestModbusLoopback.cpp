#include "SimCheck.h"
#include "FakeGpio.h"
#include "FakeLine.h"
#include "ScriptedLink.h"
#include <Drivers/Interface/Modbus/ModbusClient.h>
#include <Drivers/Interface/Modbus/ModbusServer.h>
#include <Drivers/Interface/Modbus/RTU/RtuFramer.h>
#include <Drivers/Interface/Modbus/RTU/RtuTransport.h>
#include <Utilities/Data/RegisterMap/RegisterView.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <vector>


// A client and a server joined through two simulated UARTs, one wire between them: the client's bytes arrive at the server's line
// and the server's reply arrives back. Also the client against a scripted slave, which sends replies the test chooses, including
// broken ones. Every request and reply goes through the real RTU transport, so frame timing is part of what is tested.


using Link = RtuTransport<FakeLine, FakeGpio, +[] {
	return RtuBuilder()
		.Clock<&SimClockUs>();
}>;


using Plc = ModbusClient<Link, RtuFramer, +[] {
	return ClientBuilder()
		.SlaveAddress(1)
		.Timeout(50)
		.Retries(2);
}>;


using PlcNoRetry = ModbusClient<Link, RtuFramer, +[] {
	return ClientBuilder()
		.SlaveAddress(1)
		.Timeout(50)
		.Retries(0);
}>;


// A long reply at 9600 baud takes hundreds of milliseconds, so the test that sends one waits longer.
using PlcLongReply = ModbusClient<Link, RtuFramer, +[] {
	return ClientBuilder()
		.SlaveAddress(1)
		.Timeout(1000)
		.Retries(0);
}>;


using PlcSlave2 = ModbusClient<Link, RtuFramer, +[] {
	return ClientBuilder()
		.SlaveAddress(2)
		.Timeout(20)
		.Retries(0);
}>;


uint16 setpoint = 0;
uint16 counter = 0;
bool relay = false;
uint16 readOnly = 5;


bool CheckSetpoint(uint16 value) {
	return value <= 100;
}


uint16 ReadOnlyValue() {
	return readOnly;
}


using View = RegisterView<+[] {
	return RegisterViewBuilder()
		.HoldingRegisters(0x0000, Variable(&setpoint, &CheckSetpoint))
		.HoldingRegisters(0x0002, Variable(&counter))
		.HoldingRegisters(0x0003, Computed(&ReadOnlyValue))
		.Coils(0x0000, Variable(&relay));
}>;


using Server = ModbusServer<Link, RtuFramer, View, +[] {
	return ServerBuilder()
		.SlaveAddress(1);
}>;


// The server the loopback hook runs. The hook runs the server's Process whenever simulated time advances, so a client that waits for
// a reply lets the server answer. The busy flag stops the hook from running the server again inside its own call.
Server* loopServer = nullptr;
bool loopBusy = false;


void LoopbackHook() {
	if (loopBusy || loopServer == nullptr) {
		return;
	}

	loopBusy = true;
	loopServer->Process();
	loopBusy = false;
}


// One scripted reply: its bytes, an optional gap after byte gapAfter, and an optional receive error in the middle of it. An empty
// reply is silence.
struct ScriptedReply {
	std::vector<uint8> bytes;
	uint64 gapUs = 0;
	std::size_t gapAfter = 0;
	bool errorInMiddle = false;
};


// A slave that answers the client's requests from a script. Each request the client sends takes the next reply, after a silence of
// 8 ms; the requests are recorded.
struct ScriptedSlave {
	FakeLine* line = nullptr;
	std::vector<ScriptedReply> replies;
	std::vector<std::vector<uint8>> requests;
	std::size_t handled = 0;
	std::size_t next = 0;
};


ScriptedSlave* slave = nullptr;


void SlaveHook() {
	if (slave == nullptr) {
		return;
	}

	const std::vector<FakeSentByte>& sent = slave->line->Sent();
	if (sent.size() == slave->handled) {
		return;
	}

	std::vector<uint8> request;
	for (std::size_t index = slave->handled; index < sent.size(); index++) {
		request.push_back(sent[index].value);
	}

	uint64 lastEnd = sent.back().endUs;
	slave->handled = sent.size();
	slave->requests.push_back(request);

	if (slave->next >= slave->replies.size()) {
		return;
	}

	const ScriptedReply& reply = slave->replies[slave->next];
	slave->next++;

	uint64 timeUs = lastEnd + 8000;
	for (std::size_t index = 0; index < reply.bytes.size(); index++) {
		slave->line->ScheduleByte(timeUs, reply.bytes[index]);

		if (reply.errorInMiddle && index == reply.bytes.size() / 2) {
			slave->line->ScheduleError(timeUs + 1, FakeLine::Error::Frame);
		}

		timeUs += 1145;
		if (index + 1 == reply.gapAfter) {
			timeUs += reply.gapUs;
		}
	}
}


void SetLine(FakeLine& line) {
	FakeLine::Parameters parameters;
	parameters.baudRate = 9600;
	line.SetParameters(parameters);
}


void ResetState() {
	simNowUs = 1000000;
	simCriticalDepth = 0;
	setpoint = 0;
	counter = 0;
	relay = false;
	loopServer = nullptr;
	loopBusy = false;
	slave = nullptr;
	simIdle = nullptr;
}


// A reply of the scripted kind: a whole frame, sealed with the library CRC.
ScriptedReply Frame(std::vector<uint8> body) {
	return ScriptedReply{ScriptedSeal(body), 0, 0, false};
}


// Round trip through the real server over the wire.
void TestRoundTripOverTheWire() {
	ResetState();
	FakeLine clientLine;
	FakeLine serverLine;
	SetLine(clientLine);
	SetLine(serverLine);
	FakeLine::Wire(clientLine, serverLine);
	Link clientLink(clientLine);
	Link serverLink(serverLine);
	SIM_EXPECT(clientLink.Begin() == ResultStatus::ok);
	SIM_EXPECT(serverLink.Begin() == ResultStatus::ok);
	Server server(serverLink);
	loopServer = &server;
	simIdle = &LoopbackHook;
	Plc plc(clientLink);

	SIM_EXPECT(plc.WriteSingleRegister(0x0000, 42).status == ResultStatus::ok);
	SIM_EXPECT(setpoint == 42);

	std::array<uint16, 1> readBack{};
	ModbusResult read = plc.ReadHoldingRegisters(0x0000, readBack);
	SIM_EXPECT(read.status == ResultStatus::ok && readBack[0] == 42);

	std::array<uint16, 1> counterValue = {9};
	SIM_EXPECT(plc.WriteMultipleRegisters(0x0002, counterValue).status == ResultStatus::ok);
	SIM_EXPECT(counter == 9);

	SIM_EXPECT(plc.WriteSingleCoil(0x0000, true).status == ResultStatus::ok);
	SIM_EXPECT(relay);

	std::array<bool, 1> coil{};
	SIM_EXPECT(plc.ReadCoils(0x0000, coil).status == ResultStatus::ok && coil[0]);

	simIdle = nullptr;
	loopServer = nullptr;
}


// Exception replies and a refused value come back as the client's results, over the wire.
void TestExceptionsOverTheWire() {
	ResetState();
	FakeLine clientLine;
	FakeLine serverLine;
	SetLine(clientLine);
	SetLine(serverLine);
	FakeLine::Wire(clientLine, serverLine);
	Link clientLink(clientLine);
	Link serverLink(serverLine);
	SIM_EXPECT(clientLink.Begin() == ResultStatus::ok);
	SIM_EXPECT(serverLink.Begin() == ResultStatus::ok);
	Server server(serverLink);
	loopServer = &server;
	simIdle = &LoopbackHook;
	PlcNoRetry plc(clientLink);

	std::array<uint16, 1> values{};
	ModbusResult outside = plc.ReadHoldingRegisters(0x0100, values);
	SIM_EXPECT(outside.status == ResultStatus::error && outside.exception == 2);

	ModbusResult rejected = plc.WriteSingleRegister(0x0000, 200);
	SIM_EXPECT(rejected.status == ResultStatus::error && rejected.exception == 3);
	SIM_EXPECT(setpoint == 0);

	simIdle = nullptr;
	loopServer = nullptr;
}


// A broadcast write takes effect, the server sends nothing back, and the next read sees the value.
void TestBroadcastOverTheWire() {
	ResetState();
	FakeLine clientLine;
	FakeLine serverLine;
	SetLine(clientLine);
	SetLine(serverLine);
	FakeLine::Wire(clientLine, serverLine);
	Link clientLink(clientLine);
	Link serverLink(serverLine);
	SIM_EXPECT(clientLink.Begin() == ResultStatus::ok);
	SIM_EXPECT(serverLink.Begin() == ResultStatus::ok);
	Server server(serverLink);
	loopServer = &server;
	simIdle = &LoopbackHook;
	Plc plc(clientLink);

	std::size_t before = clientLine.Sent().size();
	SIM_EXPECT(plc.Broadcast().WriteSingleRegister(0x0000, 77).status == ResultStatus::ok);
	SIM_EXPECT(clientLine.Sent().size() - before == 8);

	std::array<uint16, 1> values{};
	SIM_EXPECT(plc.ReadHoldingRegisters(0x0000, values).status == ResultStatus::ok && values[0] == 77);
	SIM_EXPECT(setpoint == 77);

	// The only frame the server sent is the reply to the read: 5 bytes of data, the address and the CRC.
	SIM_EXPECT(serverLine.Sent().size() == 7);

	simIdle = nullptr;
	loopServer = nullptr;
}


// A client whose slave address is not the server's gets no reply and times out.
void TestOtherSlaveTimesOut() {
	ResetState();
	FakeLine clientLine;
	FakeLine serverLine;
	SetLine(clientLine);
	SetLine(serverLine);
	FakeLine::Wire(clientLine, serverLine);
	Link clientLink(clientLine);
	Link serverLink(serverLine);
	SIM_EXPECT(clientLink.Begin() == ResultStatus::ok);
	SIM_EXPECT(serverLink.Begin() == ResultStatus::ok);
	Server server(serverLink);
	loopServer = &server;
	simIdle = &LoopbackHook;
	PlcSlave2 plc(clientLink);

	std::array<uint16, 1> values{};
	SIM_EXPECT(plc.ReadHoldingRegisters(0x0000, values).status == ResultStatus::timeout);
	SIM_EXPECT(serverLine.Sent().empty());

	simIdle = nullptr;
	loopServer = nullptr;
}


// Check 4: a short reply (one register for ten asked) is invalidData; each of the three attempts gets the same short reply.
void TestShortReplyOnTheLine() {
	ResetState();
	FakeLine line;
	SetLine(line);
	Link link(line);
	SIM_EXPECT(link.Begin() == ResultStatus::ok);
	ScriptedSlave script;
	script.line = &line;
	script.replies = {Frame({0x01, 0x03, 0x02, 0x00, 0x05}), Frame({0x01, 0x03, 0x02, 0x00, 0x05}), Frame({0x01, 0x03, 0x02, 0x00, 0x05})};
	slave = &script;
	simIdle = &SlaveHook;
	Plc plc(link);

	std::array<uint16, 10> values{};
	SIM_EXPECT(plc.ReadHoldingRegisters(0x0000, values).status == ResultStatus::invalidData);
	SIM_EXPECT(script.requests.size() == 3);

	simIdle = nullptr;
	slave = nullptr;
}


// Check 4: an echo that names address 51 for a request to 50 is invalidData.
void TestEchoOfWrongAddressOnTheLine() {
	ResetState();
	FakeLine line;
	SetLine(line);
	Link link(line);
	SIM_EXPECT(link.Begin() == ResultStatus::ok);
	ScriptedSlave script;
	script.line = &line;
	script.replies = {Frame({0x01, 0x06, 0x00, 0x33, 0x00, 0x07}), Frame({0x01, 0x06, 0x00, 0x33, 0x00, 0x07}), Frame({0x01, 0x06, 0x00, 0x33, 0x00, 0x07})};
	slave = &script;
	simIdle = &SlaveHook;
	Plc plc(link);

	SIM_EXPECT(plc.WriteSingleRegister(50, 7).status == ResultStatus::invalidData);

	simIdle = nullptr;
	slave = nullptr;
}


// Check 4: an exception reply is an error with its code, and it is sent once.
void TestExceptionReplyOnTheLine() {
	ResetState();
	FakeLine line;
	SetLine(line);
	Link link(line);
	SIM_EXPECT(link.Begin() == ResultStatus::ok);
	ScriptedSlave script;
	script.line = &line;
	script.replies = {Frame({0x01, 0x83, 0x02})};
	slave = &script;
	simIdle = &SlaveHook;
	Plc plc(link);

	std::array<uint16, 4> values{};
	ModbusResult result = plc.ReadHoldingRegisters(0xFFFE, values);
	SIM_EXPECT(result.status == ResultStatus::error && result.exception == 2);
	SIM_EXPECT(script.requests.size() == 1);

	simIdle = nullptr;
	slave = nullptr;
}


// Check 5: a bad CRC gives crcError.
void TestBadCrcOnTheLine() {
	ResetState();
	FakeLine line;
	SetLine(line);
	Link link(line);
	SIM_EXPECT(link.Begin() == ResultStatus::ok);
	ScriptedSlave script;
	script.line = &line;
	ScriptedReply bad = Frame({0x01, 0x03, 0x02, 0x00, 0x05});
	bad.bytes[bad.bytes.size() - 1] = static_cast<uint8>(bad.bytes[bad.bytes.size() - 1] ^ 0xFF);
	script.replies = {bad};
	slave = &script;
	simIdle = &SlaveHook;
	PlcNoRetry plc(link);

	std::array<uint16, 1> values{};
	SIM_EXPECT(plc.ReadHoldingRegisters(0x0000, values).status == ResultStatus::crcError);

	simIdle = nullptr;
	slave = nullptr;
}


// Check 5: quantity 0 is invalidParameter, and no byte reaches the line.
void TestZeroQuantityPutsNothingOnTheLine() {
	ResetState();
	FakeLine line;
	SetLine(line);
	Link link(line);
	SIM_EXPECT(link.Begin() == ResultStatus::ok);
	ScriptedSlave script;
	script.line = &line;
	slave = &script;
	simIdle = &SlaveHook;
	Plc plc(link);

	std::size_t before = line.Sent().size();
	std::array<uint16, 0> none{};
	SIM_EXPECT(plc.ReadHoldingRegisters(0x0000, std::span<uint16>(none)).status == ResultStatus::invalidParameter);
	SIM_EXPECT(line.Sent().size() == before);

	simIdle = nullptr;
	slave = nullptr;
}


// Check 5: a receive error event in the middle of the reply gives invalidData.
void TestReceiveErrorGivesInvalidData() {
	ResetState();
	FakeLine line;
	SetLine(line);
	Link link(line);
	SIM_EXPECT(link.Begin() == ResultStatus::ok);
	ScriptedSlave script;
	script.line = &line;
	ScriptedReply broken = Frame({0x01, 0x03, 0x04, 0x00, 0x01, 0x00, 0x02});
	broken.errorInMiddle = true;
	script.replies = {broken};
	slave = &script;
	simIdle = &SlaveHook;
	PlcNoRetry plc(link);

	std::array<uint16, 2> values{};
	SIM_EXPECT(plc.ReadHoldingRegisters(0x0000, values).status == ResultStatus::invalidData);

	simIdle = nullptr;
	slave = nullptr;
}


// Check 5: a reply longer than the receive ring (256 entries) overflows it, and the frame breaks: invalidData. The reply is 259 bytes
// (257 bytes of body and the CRC), which no Modbus frame is allowed to be.
void TestFullRingGivesInvalidData() {
	ResetState();
	FakeLine line;
	SetLine(line);
	Link link(line);
	SIM_EXPECT(link.Begin() == ResultStatus::ok);
	ScriptedSlave script;
	script.line = &line;
	std::vector<uint8> body = {0x01, 0x03, 0xFE};
	for (int index = 0; index < 254; index++) {
		body.push_back(0x00);
	}
	script.replies = {Frame(body)};
	slave = &script;
	simIdle = &SlaveHook;
	PlcLongReply plc(link);

	std::array<uint16, 124> values{};
	SIM_EXPECT(plc.ReadHoldingRegisters(0x0000, values).status == ResultStatus::invalidData);

	simIdle = nullptr;
	slave = nullptr;
}


// Frame end: a gap between t1.5 and t3.5 inside a reply breaks it. The retry gets the good reply.
void TestGapInsideReplyBreaksItThenRetryWorks() {
	ResetState();
	FakeLine line;
	SetLine(line);
	Link link(line);
	SIM_EXPECT(link.Begin() == ResultStatus::ok);
	std::vector<uint8> body = {0x01, 0x03, 0x02, 0x00, 0x05};
	ScriptedReply gapped = Frame(body);
	gapped.gapUs = 2500;
	gapped.gapAfter = 2;
	ScriptedSlave script;
	script.line = &line;
	script.replies = {gapped, Frame(body)};
	slave = &script;
	simIdle = &SlaveHook;
	Plc plc(link);

	std::array<uint16, 1> values{};
	SIM_EXPECT(plc.ReadHoldingRegisters(0x0000, values).status == ResultStatus::ok);
	SIM_EXPECT(values[0] == 5);
	SIM_EXPECT(script.requests.size() == 2);

	simIdle = nullptr;
	slave = nullptr;
}


// A silent slave: the client times out once per attempt, sends the same request each time, and gives timeout.
void TestSilenceTimesOutOnTheLine() {
	ResetState();
	FakeLine line;
	SetLine(line);
	Link link(line);
	SIM_EXPECT(link.Begin() == ResultStatus::ok);
	ScriptedSlave script;
	script.line = &line;
	script.replies = {ScriptedReply{}, ScriptedReply{}, ScriptedReply{}};
	slave = &script;
	simIdle = &SlaveHook;
	Plc plc(link);

	std::array<uint16, 1> values{};
	SIM_EXPECT(plc.ReadHoldingRegisters(0x0000, values).status == ResultStatus::timeout);
	SIM_EXPECT(script.requests.size() == 3);
	SIM_EXPECT(script.requests[0] == script.requests[2]);

	simIdle = nullptr;
	slave = nullptr;
}


// The line echoes every byte the client sends (no RE pin). The echo arrives during the send and the clear after the hold drops it,
// so the reply is read cleanly.
void TestEchoOnTheLineIsDropped() {
	ResetState();
	FakeLine line;
	SetLine(line);
	line.SetEcho(true);
	Link link(line);
	SIM_EXPECT(link.Begin() == ResultStatus::ok);
	ScriptedSlave script;
	script.line = &line;
	script.replies = {Frame({0x01, 0x03, 0x02, 0x00, 0x05})};
	slave = &script;
	simIdle = &SlaveHook;
	PlcNoRetry plc(link);

	std::array<uint16, 1> values{};
	SIM_EXPECT(plc.ReadHoldingRegisters(0x0000, values).status == ResultStatus::ok);
	SIM_EXPECT(values[0] == 5);

	simIdle = nullptr;
	slave = nullptr;
}


// The write reports timeout after the request's bytes have left the line. The transport says writeError, so the client does not send
// the request again even though Retries is 2: one 8-byte frame is on the line, and the result is writeError.
void TestWriteErrorIsNotRetried() {
	ResetState();
	FakeLine line;
	SetLine(line);
	line.SetWriteTimeout(true);
	Link link(line);
	SIM_EXPECT(link.Begin() == ResultStatus::ok);
	Plc plc(link);

	std::array<uint16, 1> values{};
	SIM_EXPECT(plc.ReadHoldingRegisters(0x0000, values).status == ResultStatus::writeError);
	SIM_EXPECT(line.Sent().size() == 8);
}


int main() {
	TestRoundTripOverTheWire();
	TestExceptionsOverTheWire();
	TestBroadcastOverTheWire();
	TestOtherSlaveTimesOut();
	TestShortReplyOnTheLine();
	TestEchoOfWrongAddressOnTheLine();
	TestExceptionReplyOnTheLine();
	TestBadCrcOnTheLine();
	TestZeroQuantityPutsNothingOnTheLine();
	TestReceiveErrorGivesInvalidData();
	TestFullRingGivesInvalidData();
	TestGapInsideReplyBreaksItThenRetryWorks();
	TestSilenceTimesOutOnTheLine();
	TestEchoOnTheLineIsDropped();
	TestWriteErrorIsNotRetried();
	return SimFinish("TestModbusLoopback");
}
