#include "SimCheck.h"
#include "Chains.h"
#include "Doubles.h"
#include <Drivers/Interface/VUB/TestKit/Contracts.h>
#include <Drivers/Interface/VUB/TestKit/Loopback.h>
#include <Drivers/Interface/VUB/Transform/Crc.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>


// The nine check bytes "123456789", and the packet a CRC-16/MODBUS transform makes of them. The catalogue's check value of MODBUS is
// 4B37; on the wire it is sent low byte first, so the bytes after the digits are 37 then 4B.
const std::array<uint8, 9> digits = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39};


// Encode of the digits by hand: the nine bytes, then 37 4B (the check value 4B37, low byte first).
void TestModbusEncodesLowByteFirst() {
	VUB::Crc16Modbus transform;
	VUB::Meta meta{};
	std::array<uint8, 11> wire{};
	Result<std::size_t> encoded = transform.Encode(meta, std::span<const uint8>(digits.data(), digits.size()), wire);
	SIM_EXPECT(encoded.IsOk());
	SIM_EXPECT(encoded.Value() == 11);
	const std::array<uint8, 11> expected = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x37, 0x4B};
	SIM_EXPECT(wire == expected);
}


// The high byte first variant sends the same CRC as 4B 37.
void TestModbusEncodesHighByteFirst() {
	VUB::Crc16ModbusHighByteFirst transform;
	VUB::Meta meta{};
	std::array<uint8, 11> wire{};
	SIM_EXPECT(transform.Encode(meta, std::span<const uint8>(digits.data(), digits.size()), wire).IsOk());
	const std::array<uint8, 11> expected = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x4B, 0x37};
	SIM_EXPECT(wire == expected);
}


// The CRC-8 preset of the digits is F4, one byte.
void TestCrc8SmbusEncodes() {
	VUB::Crc8Smbus transform;
	VUB::Meta meta{};
	std::array<uint8, 10> wire{};
	SIM_EXPECT(transform.Encode(meta, std::span<const uint8>(digits.data(), digits.size()), wire).IsOk());
	const std::array<uint8, 10> expected = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0xF4};
	SIM_EXPECT(wire == expected);
}


// The CRC-32 preset of the digits is CBF43926, low byte first: 26 39 F4 CB.
void TestCrc32IsoHdlcEncodes() {
	VUB::Crc32IsoHdlc transform;
	VUB::Meta meta{};
	std::array<uint8, 13> wire{};
	SIM_EXPECT(transform.Encode(meta, std::span<const uint8>(digits.data(), digits.size()), wire).IsOk());
	const std::array<uint8, 13> expected = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x26, 0x39, 0xF4, 0xCB};
	SIM_EXPECT(wire == expected);
}


// Decode of the wire bytes by hand gives the digits back, in place, and the length of the body.
void TestModbusDecodesWireBytes() {
	VUB::Crc16Modbus transform;
	VUB::Meta meta{};
	std::array<uint8, 11> wire = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x37, 0x4B};
	Result<std::size_t> decoded = transform.Decode(meta, std::span<uint8>(wire.data(), wire.size()));
	SIM_EXPECT(decoded.IsOk());
	SIM_EXPECT(decoded.Value() == 9);
	for (std::size_t index = 0; index < digits.size(); index++) {
		SIM_EXPECT(wire[index] == digits[index]);
	}
}


// The same bytes read as high byte first carry 374B, not 4B37: a crcError, and the byte order is part of the wire.
void TestModbusByteOrderIsChecked() {
	VUB::Crc16ModbusHighByteFirst transform;
	VUB::Meta meta{};
	std::array<uint8, 11> wire = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x37, 0x4B};
	Result<std::size_t> decoded = transform.Decode(meta, std::span<uint8>(wire.data(), wire.size()));
	SIM_EXPECT(decoded.IsErr());
	SIM_EXPECT(decoded.Error() == ResultStatus::crcError);
}


// One changed payload byte is a crcError, and a packet shorter than the CRC is an invalid data error.
void TestDamagedPacketsAreRefused() {
	VUB::Crc16Modbus transform;
	VUB::Meta meta{};
	std::array<uint8, 11> wire = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x37, 0x4B};
	wire[4] = 0x25;
	Result<std::size_t> decoded = transform.Decode(meta, std::span<uint8>(wire.data(), wire.size()));
	SIM_EXPECT(decoded.IsErr());
	SIM_EXPECT(decoded.Error() == ResultStatus::crcError);

	std::array<uint8, 1> shortPacket = {0x37};
	Result<std::size_t> tooShort = transform.Decode(meta, std::span<uint8>(shortPacket.data(), shortPacket.size()));
	SIM_EXPECT(tooShort.IsErr());
	SIM_EXPECT(tooShort.Error() == ResultStatus::invalidData);
}


// The contract passes for the presets with the table engine, and for one of them with the bitwise engine.
void TestPresetsPassContract() {
	VUB::Crc16Modbus modbus;
	SIM_EXPECT(VUB::CheckTransform(modbus, 7, 500, 40));
	VUB::Crc<VUB::Crc16Modbus::parameters, VUB::CrcBitwise> modbusBitwise;
	SIM_EXPECT(VUB::CheckTransform(modbusBitwise, 8, 500, 40));
	VUB::Crc8Smbus smbus;
	SIM_EXPECT(VUB::CheckTransform(smbus, 9, 500, 40));
	VUB::Crc32IsoHdlc isoHdlc;
	SIM_EXPECT(VUB::CheckTransform(isoHdlc, 10, 500, 255));
	VUB::Crc16ModbusHighByteFirst modbusHighFirst;
	SIM_EXPECT(VUB::CheckTransform(modbusHighFirst, 11, 500, 40));
	VUB::Crc16Riello riello;
	SIM_EXPECT(VUB::CheckTransform(riello, 12, 500, 40));
}


// The custom set of the presets test, high byte first: 45 74 after the digits. The bytes come from the reference's CRC 4574.
void TestCustomHighByteFirstEncodes() {
	VUB::CrcHighByteFirst<VUB::CrcParameters{16, 0x1021, 0x1D0F, 0x00FF, true, false, 0x4574}> transform;
	VUB::Meta meta{};
	std::array<uint8, 11> wire{};
	SIM_EXPECT(transform.Encode(meta, std::span<const uint8>(digits.data(), digits.size()), wire).IsOk());
	const std::array<uint8, 11> expected = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x45, 0x74};
	SIM_EXPECT(wire == expected);
}


// A stack over the faulty loopback with flag framing: every payload the handler gets is one that was sent, whole, and the lost or
// changed frames are counted. A corrupted frame that the framing lets through is a transformError, and none reaches the handler.
using CrcStreamEnd = VUB::StreamEnd<64, 512>;


using CrcStream = VUB::Stack<+[] {
	return VUB::Builder()
		.Transform<VUB::Crc16Modbus>()
		.Framing<FlagFraming>()
		.Link<CrcStreamEnd>()
		.MaxPayload(40)
		.ReceiveRing(256)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


void TestFaultyLoopbackNeverDeliversCorruptedPayloads() {
	ResetTestState();
	VUB::Faults faults;
	faults.seed = 33;
	faults.dropPercent = 5;
	faults.corruptPercent = 8;
	faults.duplicatePercent = 5;
	faults.maxDelayUs = 20;
	StreamLine line(faults);
	CrcStream sender(line.a);
	CrcStream receiver(line.b);

	std::size_t sentCount = 0;
	for (uint32 step = 0; step < 600; step++) {
		testNowUs = step * 10;
		line.SetNow(step * 10);
		if (sentCount < 120) {
			std::array<uint8, 4> payload = {static_cast<uint8>(sentCount), 0x10, 0x20, 0x30};
			if (sender.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok) {
				sentCount++;
			}
		}

		sender.Process();
		line.DeliverToB(receiver);
		receiver.Process();
	}

	VUB::WireCounts counts = line.toB.Counts();
	SIM_EXPECT(counts.corrupted > 0);
	for (std::size_t index = 0; index < testReceivedCount && index < 256; index++) {
		SIM_EXPECT(testReceivedLength[index] == 4);
		SIM_EXPECT(testReceived[index][0] < sentCount);
		SIM_EXPECT(testReceived[index][1] == 0x10);
		SIM_EXPECT(testReceived[index][2] == 0x20);
		SIM_EXPECT(testReceived[index][3] == 0x30);
	}

	VUB::DropCounters drops = receiver.Drops();
	SIM_EXPECT(drops.transformError + drops.framingError > 0);
}


// The same CRC over a packet link with faults: a packet the wire corrupts reaches the CRC, so each one is a transformError, and the
// packets the handler gets are whole. Every packet the wire delivers ends in the handler or in one counted reason.
using CrcPacketStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Transform<VUB::Crc16Modbus>()
		.Link<PacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


void TestPacketFaultsCountTransformErrors() {
	ResetTestState();
	VUB::Faults faults;
	faults.seed = 41;
	faults.dropPercent = 10;
	faults.corruptPercent = 10;
	faults.duplicatePercent = 10;
	faults.maxDelayUs = 30;
	PacketLine line(faults);
	CrcPacketStack sender(line.a);
	CrcPacketStack receiver(line.b);

	std::size_t sentCount = 0;
	for (uint32 step = 0; step < 400; step++) {
		testNowUs = step * 10;
		line.SetNow(step * 10);
		if (sentCount < 100) {
			std::array<uint8, 4> payload = {static_cast<uint8>(sentCount), 0x10, 0x20, 0x30};
			if (sender.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok) {
				sentCount++;
			}
		}

		sender.Process();
		line.DeliverToB(receiver);
		receiver.Process();
	}

	VUB::WireCounts counts = line.toB.Counts();
	SIM_EXPECT(counts.corrupted > 0);
	VUB::DropCounters drops = receiver.Drops();
	SIM_EXPECT(drops.transformError > 0);
	for (std::size_t index = 0; index < testReceivedCount && index < 256; index++) {
		SIM_EXPECT(testReceivedLength[index] == 4);
		SIM_EXPECT(testReceived[index][0] < sentCount);
		SIM_EXPECT(testReceived[index][1] == 0x10);
		SIM_EXPECT(testReceived[index][2] == 0x20);
		SIM_EXPECT(testReceived[index][3] == 0x30);
	}

	std::size_t stackDrops = drops.transformError + drops.layerError + drops.tooLong + drops.queueFull;
	SIM_EXPECT(testReceivedCount + stackDrops == counts.delivered);
}


// A frame that the flag framing accepts, whose CRC bytes are wrong, is a transformError and reaches no handler. The frame is made by
// the framing double: the digits, then 00 00 where the CRC of the digits (37 4B) belongs.
void TestWrongCrcIsCountedAsTransformError() {
	ResetTestState();
	StreamLine line(VUB::Faults{});
	CrcStream receiver(line.b);

	std::array<uint8, 11> packet = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x00, 0x00};
	FlagFraming framing;
	VUB::Meta meta{};
	std::array<uint8, 64> frame{};
	Result<std::size_t> framed = framing.Frame(meta, std::span<const uint8>(packet.data(), packet.size()), frame);
	SIM_EXPECT(framed.IsOk());
	for (std::size_t index = 0; index < framed.Value(); index++) {
		receiver.Feed(frame[index]);
	}

	receiver.Process();
	SIM_EXPECT(testReceivedCount == 0);
	VUB::DropCounters drops = receiver.Drops();
	SIM_EXPECT(drops.transformError == 1);
	SIM_EXPECT(drops.framingError == 0);
}


// A chain with the high byte first variant puts 4B 37 after the digits on the wire; the byte after the flag is the first digit.
using HighFirstStream = VUB::Stack<+[] {
	return VUB::Builder()
		.Transform<VUB::Crc16ModbusHighByteFirst>()
		.Framing<FlagFraming>()
		.Link<CrcStreamEnd>()
		.MaxPayload(40)
		.ReceiveRing(256)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


void TestChainByteOrderReachesTheWire() {
	ResetTestState();
	StreamLine line(VUB::Faults{});
	HighFirstStream sender(line.a);
	SIM_EXPECT(sender.Send(std::span<const uint8>(digits.data(), digits.size())) == ResultStatus::ok);
	sender.Process();

	RecordSink sink;
	line.DeliverToB(sink);
	SIM_EXPECT(sink.byteCount > 12);
	SIM_EXPECT(sink.bytes[0] == 0x7E);
	for (std::size_t index = 0; index < digits.size(); index++) {
		SIM_EXPECT(sink.bytes[1 + index] == digits[index]);
	}

	SIM_EXPECT(sink.bytes[10] == 0x4B);
	SIM_EXPECT(sink.bytes[11] == 0x37);
}


int main() {
	TestModbusEncodesLowByteFirst();
	TestModbusEncodesHighByteFirst();
	TestCrc8SmbusEncodes();
	TestCrc32IsoHdlcEncodes();
	TestModbusDecodesWireBytes();
	TestModbusByteOrderIsChecked();
	TestDamagedPacketsAreRefused();
	TestPresetsPassContract();
	TestFaultyLoopbackNeverDeliversCorruptedPayloads();
	TestPacketFaultsCountTransformErrors();
	TestWrongCrcIsCountedAsTransformError();
	TestChainByteOrderReachesTheWire();
	TestCustomHighByteFirstEncodes();
	return SimFinish("TestVubCrcWire");
}
