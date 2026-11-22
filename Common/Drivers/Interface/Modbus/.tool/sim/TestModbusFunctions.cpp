#include "SimCheck.h"
#include "ScriptedLink.h"
#include <Drivers/Interface/Modbus/ModbusClient.h>
#include <Drivers/Interface/Modbus/RTU/RtuFramer.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>
#include <vector>


// The client's function by function, over a scripted link. The expected frames are the spec example (01 03 00 00 00 0A C5 CD) and
// frames worked by hand from the function layouts, sealed with the library CRC (ScriptedSeal).

using Plc = ModbusClient<ScriptedLink, RtuFramer, +[] {
	return ClientBuilder()
		.SlaveAddress(1)
		.Timeout(20)
		.Retries(2);
}>;

using PlcNoRetry = ModbusClient<ScriptedLink, RtuFramer, +[] {
	return ClientBuilder()
		.SlaveAddress(1)
		.Timeout(20)
		.Retries(0);
}>;


struct CountingLock {
	int locks = 0;
	int unlocks = 0;


	void Lock() {
		locks++;
	}


	void Unlock() {
		unlocks++;
	}
};


using PlcLocked = ModbusClient<ScriptedLink, RtuFramer, +[] {
	return ClientBuilder()
		.SlaveAddress(1)
		.Timeout(20)
		.Retries(0)
		.Lock();
}, CountingLock>;


bool Is(const ModbusResult& result, ResultStatus status, uint8 exception = 0) {
	return result.status == status && result.exception == exception;
}


// Pushes the same reply once per attempt of a client with two retries.
void PushEveryAttempt(ScriptedLink& link, const std::vector<uint8>& frame) {
	link.PushFrame(frame);
	link.PushFrame(frame);
	link.PushFrame(frame);
}


void TestReadHoldingSpecFrame() {
	ScriptedLink link;
	Plc plc(link);
	std::array<uint16, 10> values{};

	std::vector<uint8> reply = {0x01, 0x03, 0x14};
	for (uint8 value = 1; value <= 10; value++) {
		reply.push_back(0);
		reply.push_back(value);
	}
	link.PushFrame(ScriptedSeal(reply));

	ModbusResult result = plc.ReadHoldingRegisters(0x0000, values);
	SIM_EXPECT(Is(result, ResultStatus::ok));
	SIM_EXPECT(link.Sent().size() == 1);
	SIM_EXPECT((link.Sent()[0] == std::vector<uint8>{0x01, 0x03, 0x00, 0x00, 0x00, 0x0A, 0xC5, 0xCD}));
	SIM_EXPECT(values[0] == 1 && values[9] == 10);
	SIM_EXPECT(link.Timeouts().size() == 1 && link.Timeouts()[0] == 20000);
}


// Check 4: ten registers asked, one sent. The byte count is wrong for the quantity, so the reply is invalidData, and it is retried.
void TestShortReplyIsInvalidData() {
	ScriptedLink link;
	Plc plc(link);
	std::array<uint16, 10> values{};
	PushEveryAttempt(link, ScriptedSeal({0x01, 0x03, 0x02, 0x00, 0x05}));

	SIM_EXPECT(Is(plc.ReadHoldingRegisters(0x0000, values), ResultStatus::invalidData));
	SIM_EXPECT(link.Sent().size() == 3);
}


// Check 4: a write echo that names address 51 for a request to 50 is invalidData.
void TestEchoMismatchIsInvalidData() {
	ScriptedLink link;
	Plc plc(link);
	PushEveryAttempt(link, ScriptedSeal({0x01, 0x06, 0x00, 0x33, 0x00, 0x07}));

	SIM_EXPECT(Is(plc.WriteSingleRegister(50, 7), ResultStatus::invalidData));
	SIM_EXPECT(link.Sent().size() == 3);
	std::vector<uint8> expected = ScriptedSeal({0x01, 0x06, 0x00, 0x32, 0x00, 0x07});
	SIM_EXPECT((link.Sent()[0] == expected));
}


// Check 4: exception 02 is an error with the exception code, and it is not retried.
void TestExceptionIsNotRetried() {
	ScriptedLink link;
	Plc plc(link);
	std::array<uint16, 4> values{};
	link.PushFrame(ScriptedSeal({0x01, 0x83, 0x02}));

	ModbusResult result = plc.ReadHoldingRegisters(0xFFFE, values);
	SIM_EXPECT(Is(result, ResultStatus::error, 2));
	SIM_EXPECT(link.Sent().size() == 1);
}


// A write exception is returned the same way.
void TestWriteExceptionIsNotRetried() {
	ScriptedLink link;
	Plc plc(link);
	link.PushFrame(ScriptedSeal({0x01, 0x86, 0x03}));

	SIM_EXPECT(Is(plc.WriteSingleRegister(1, 1), ResultStatus::error, 3));
	SIM_EXPECT(link.Sent().size() == 1);
}


// Check 1: a CRC error in the reply.
void TestBadCrcIsCrcError() {
	ScriptedLink link;
	PlcNoRetry plc(link);
	std::array<uint16, 1> values{};
	std::vector<uint8> reply = ScriptedSeal({0x01, 0x03, 0x02, 0x00, 0x05});
	reply[reply.size() - 1] = static_cast<uint8>(reply[reply.size() - 1] ^ 0xFF);
	link.PushFrame(reply);

	SIM_EXPECT(Is(plc.ReadHoldingRegisters(0x0000, values), ResultStatus::crcError));
	SIM_EXPECT(link.Sent().size() == 1);
}


// Check 2: a reply from another slave is invalidData.
void TestOtherSlaveReplyIsInvalidData() {
	ScriptedLink link;
	PlcNoRetry plc(link);
	std::array<uint16, 1> values{};
	link.PushFrame(ScriptedSeal({0x02, 0x03, 0x02, 0x00, 0x05}));

	SIM_EXPECT(Is(plc.ReadHoldingRegisters(0x0000, values), ResultStatus::invalidData));
}


// Check 5: quantity 0 and a quantity over the limit are invalidParameter, and nothing is sent.
void TestBadQuantityIsNotSent() {
	ScriptedLink link;
	Plc plc(link);
	std::vector<uint16> none;
	std::vector<uint16> tooMany(126);

	SIM_EXPECT(Is(plc.ReadHoldingRegisters(0, std::span<uint16>(none)), ResultStatus::invalidParameter));
	SIM_EXPECT(Is(plc.ReadHoldingRegisters(0, std::span<uint16>(tooMany)), ResultStatus::invalidParameter));
	SIM_EXPECT(Is(plc.WriteMultipleRegisters(0, std::span<const uint16>(none)), ResultStatus::invalidParameter));
	SIM_EXPECT(link.Sent().empty());
}


// Retries: a timeout is sent again, and the retry's reply is returned. Each attempt sends the same bytes.
void TestRetryThenReply() {
	ScriptedLink link;
	Plc plc(link);
	std::array<uint16, 1> values{};
	std::vector<uint8> bad = ScriptedSeal({0x01, 0x03, 0x02, 0x00, 0x05});
	bad[bad.size() - 1] = static_cast<uint8>(bad[bad.size() - 1] ^ 0xFF);
	link.PushFrame(bad);
	link.PushFrame(ScriptedSeal({0x01, 0x03, 0x02, 0x00, 0x05}));

	SIM_EXPECT(Is(plc.ReadHoldingRegisters(0x0000, values), ResultStatus::ok));
	SIM_EXPECT(link.Sent().size() == 2);
	SIM_EXPECT(link.Sent()[0] == link.Sent()[1]);
	SIM_EXPECT(values[0] == 5);
}


// Retries: silence on every attempt gives timeout after three sends.
void TestSilenceGivesTimeoutAfterRetries() {
	ScriptedLink link;
	Plc plc(link);
	std::array<uint16, 1> values{};

	SIM_EXPECT(Is(plc.ReadHoldingRegisters(0x0000, values), ResultStatus::timeout));
	SIM_EXPECT(link.Sent().size() == 3);
}


// A transport failure is not retried.
void TestReadErrorIsNotRetried() {
	ScriptedLink link;
	Plc plc(link);
	std::array<uint16, 1> values{};
	link.PushStatus(ResultStatus::readError);

	SIM_EXPECT(Is(plc.ReadHoldingRegisters(0x0000, values), ResultStatus::readError));
	SIM_EXPECT(link.Sent().size() == 1);
}


void TestSendFailureIsNotRetried() {
	ScriptedLink link;
	link.SetSendStatus(ResultStatus::writeError);
	Plc plc(link);

	SIM_EXPECT(Is(plc.WriteSingleCoil(1, true), ResultStatus::writeError));
	SIM_EXPECT(link.Sent().size() == 1);
}


void TestWriteSingleCoilFrame() {
	ScriptedLink link;
	Plc plc(link);
	link.PushFrame(ScriptedSeal({0x01, 0x05, 0x00, 0xAC, 0xFF, 0x00}));

	SIM_EXPECT(Is(plc.WriteSingleCoil(0x00AC, true), ResultStatus::ok));
	SIM_EXPECT((link.Sent()[0] == ScriptedSeal({0x01, 0x05, 0x00, 0xAC, 0xFF, 0x00})));
}


void TestWriteMultipleRegistersFrame() {
	ScriptedLink link;
	Plc plc(link);
	std::array<uint16, 2> values = {0x000A, 0x0102};
	link.PushFrame(ScriptedSeal({0x01, 0x10, 0x00, 0x01, 0x00, 0x02}));

	SIM_EXPECT(Is(plc.WriteMultipleRegisters(1, values), ResultStatus::ok));
	SIM_EXPECT((link.Sent()[0] == ScriptedSeal({0x01, 0x10, 0x00, 0x01, 0x00, 0x02, 0x04, 0x00, 0x0A, 0x01, 0x02})));
}


// Ten coils from 0x0013: the values 1,0,1,1,0,0,1,1 give 0xCD, and 0,1 give 0x02 (coil 9 is set).
void TestWriteMultipleCoilsFrame() {
	ScriptedLink link;
	Plc plc(link);
	std::array<bool, 10> values = {true, false, true, true, false, false, true, true, false, true};
	link.PushFrame(ScriptedSeal({0x01, 0x0F, 0x00, 0x13, 0x00, 0x0A}));

	SIM_EXPECT(Is(plc.WriteMultipleCoils(0x0013, values), ResultStatus::ok));
	SIM_EXPECT((link.Sent()[0] == ScriptedSeal({0x01, 0x0F, 0x00, 0x13, 0x00, 0x0A, 0x02, 0xCD, 0x02})));
}


void TestReadCoilsBits() {
	ScriptedLink link;
	Plc plc(link);
	std::array<bool, 10> values{};
	link.PushFrame(ScriptedSeal({0x01, 0x01, 0x02, 0xCD, 0x02}));

	SIM_EXPECT(Is(plc.ReadCoils(0x0013, values), ResultStatus::ok));
	SIM_EXPECT(values[0] && !values[1] && values[2] && values[7] && values[9] && !values[8]);
	SIM_EXPECT((link.Sent()[0] == ScriptedSeal({0x01, 0x01, 0x00, 0x13, 0x00, 0x0A})));
}


// Broadcast: the frame goes to address 0, no reply is read, and the result is ok after the send.
void TestBroadcastWriteFrame() {
	ScriptedLink link;
	Plc plc(link);

	SIM_EXPECT(Is(plc.Broadcast().WriteSingleRegister(0x0000, 1234), ResultStatus::ok));
	SIM_EXPECT(link.Sent().size() == 1);
	SIM_EXPECT((link.Sent()[0] == ScriptedSeal({0x00, 0x06, 0x00, 0x00, 0x04, 0xD2})));
	SIM_EXPECT(link.Timeouts().empty());
}


void TestBroadcastCoilAndRegistersFrames() {
	ScriptedLink link;
	Plc plc(link);
	std::array<uint16, 2> values = {1, 2};

	SIM_EXPECT(Is(plc.Broadcast().WriteSingleCoil(0x0001, false), ResultStatus::ok));
	SIM_EXPECT((link.Sent()[0] == ScriptedSeal({0x00, 0x05, 0x00, 0x01, 0x00, 0x00})));
	SIM_EXPECT(Is(plc.Broadcast().WriteMultipleRegisters(0x0002, values), ResultStatus::ok));
	SIM_EXPECT((link.Sent()[1] == ScriptedSeal({0x00, 0x10, 0x00, 0x02, 0x00, 0x02, 0x04, 0x00, 0x01, 0x00, 0x02})));
	SIM_EXPECT(link.Timeouts().empty());
}


void TestBroadcastEmptyIsNotSent() {
	ScriptedLink link;
	Plc plc(link);
	std::vector<uint16> none;

	SIM_EXPECT(Is(plc.Broadcast().WriteMultipleRegisters(0, std::span<const uint16>(none)), ResultStatus::invalidParameter));
	SIM_EXPECT(link.Sent().empty());
}


// Lock: each request takes and releases the lock once, also when the request is refused before it is sent.
void TestLockAroundEachRequest() {
	ScriptedLink link;
	CountingLock lock;
	PlcLocked plc(link, &lock);
	std::array<uint16, 1> values{};
	link.PushFrame(ScriptedSeal({0x01, 0x03, 0x02, 0x00, 0x05}));

	SIM_EXPECT(Is(plc.ReadHoldingRegisters(0x0000, values), ResultStatus::ok));
	SIM_EXPECT(Is(plc.ReadHoldingRegisters(0x0000, std::span<uint16>()), ResultStatus::invalidParameter));
	SIM_EXPECT(lock.locks == 2 && lock.unlocks == 2);
}


// A locked client built without a lock object answers noInit and sends nothing. It does not dereference the missing lock.
void TestLockedClientWithoutLockObject() {
	ScriptedLink link;
	PlcLocked plc(link);
	std::array<uint16, 1> values{};

	SIM_EXPECT(Is(plc.ReadHoldingRegisters(0x0000, values), ResultStatus::noInit));
	SIM_EXPECT(link.Sent().empty());
}


// The longest timeout the builder takes is the largest whole number of milliseconds whose microseconds fit in 32 bits.
void TestLargestTimeoutIsAccepted() {
	constexpr ClientBuilder largest = ClientBuilder().Timeout(4294967);
	SIM_EXPECT(largest.timeoutMs == 4294967);
}


// The span read copies the registers of the reply.
void TestSpanReadCopiesValues() {
	ScriptedLink link;
	Plc plc(link);
	std::vector<uint16> values(2);
	link.PushFrame(ScriptedSeal({0x01, 0x03, 0x04, 0x12, 0x34, 0xAB, 0xCD}));

	SIM_EXPECT(Is(plc.ReadHoldingRegisters(0x0000, std::span<uint16>(values)), ResultStatus::ok));
	SIM_EXPECT(values[0] == 0x1234 && values[1] == 0xABCD);
}


int main() {
	TestReadHoldingSpecFrame();
	TestShortReplyIsInvalidData();
	TestEchoMismatchIsInvalidData();
	TestExceptionIsNotRetried();
	TestWriteExceptionIsNotRetried();
	TestBadCrcIsCrcError();
	TestOtherSlaveReplyIsInvalidData();
	TestBadQuantityIsNotSent();
	TestRetryThenReply();
	TestSilenceGivesTimeoutAfterRetries();
	TestReadErrorIsNotRetried();
	TestSendFailureIsNotRetried();
	TestWriteSingleCoilFrame();
	TestWriteMultipleRegistersFrame();
	TestWriteMultipleCoilsFrame();
	TestReadCoilsBits();
	TestBroadcastWriteFrame();
	TestBroadcastCoilAndRegistersFrames();
	TestBroadcastEmptyIsNotSent();
	TestLockAroundEachRequest();
	TestLockedClientWithoutLockObject();
	TestLargestTimeoutIsAccepted();
	TestSpanReadCopiesValues();
	return SimFinish("TestModbusFunctions");
}
