#include "SimCheck.h"
#include "Chains.h"
#include "Doubles.h"
#include <Drivers/Interface/VUB/TestKit/Contracts.h>
#include <Drivers/Interface/VUB/TestKit/Loopback.h>
#include <Drivers/Interface/VUB/Transform/Crc.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <Utilities/Checksum/CRC/Crc.h>
#include <Utilities/Checksum/CRC/Engine/CrcTable.h>
#include <Utilities/Checksum/CRC/Preset/Crc5Usb.h>
#include <Utilities/Checksum/CRC/Preset/Crc8Smbus.h>
#include <Utilities/Checksum/CRC/Preset/Crc12Umts.h>
#include <Utilities/Checksum/CRC/Preset/Crc16Modbus.h>
#include <Utilities/Checksum/CRC/Preset/Crc16Riello.h>
#include <Utilities/Checksum/CRC/Preset/Crc16Tms37157.h>
#include <Utilities/Checksum/CRC/Preset/Crc16Usb.h>
#include <Utilities/Checksum/CRC/Preset/Crc24OpenPgp.h>
#include <Utilities/Checksum/CRC/Preset/Crc32IsoHdlc.h>
#include <Utilities/Checksum/CRC/Preset/Crc64Ecma182.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>
#include <type_traits>


// The nine check bytes "123456789", and the packet a CRC-16/MODBUS transform makes of them. The catalogue's check value of MODBUS is
// 4B37; on the wire it is sent low byte first, so the bytes after the digits are 37 then 4B.
const std::array<uint8, 9> digits = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39};


// A CRC parameter set that is not a preset: reflectIn differs from reflectOut, and the initial value is not symmetric. Its check value
// is the CRC of the digits, 4574.
inline constexpr Checksum::CrcParameters customParameters = Checksum::CrcParameters()
	.Width(16)
	.Polynomial(0x1021)
	.Initial(0x1D0F)
	.FinalXor(0x00FF)
	.ReflectIn()
	.Check(0x4574);


// Encode of the digits by hand: the nine bytes, then 37 4B (the check value 4B37, low byte first).
void TestModbusEncodesLowByteFirst() {
	VUB::Crc<Checksum::Crc16Modbus> transform;
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
	VUB::Crc<Checksum::Crc16Modbus, VUB::CrcOrder::highByteFirst> transform;
	VUB::Meta meta{};
	std::array<uint8, 11> wire{};
	SIM_EXPECT(transform.Encode(meta, std::span<const uint8>(digits.data(), digits.size()), wire).IsOk());
	const std::array<uint8, 11> expected = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x4B, 0x37};
	SIM_EXPECT(wire == expected);
}


// The CRC-8 of the digits is F4, one byte.
void TestCrc8SmbusEncodes() {
	VUB::Crc<Checksum::Crc8Smbus> transform;
	VUB::Meta meta{};
	std::array<uint8, 10> wire{};
	SIM_EXPECT(transform.Encode(meta, std::span<const uint8>(digits.data(), digits.size()), wire).IsOk());
	const std::array<uint8, 10> expected = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0xF4};
	SIM_EXPECT(wire == expected);
}


// The CRC-32 of the digits is CBF43926, low byte first: 26 39 F4 CB.
void TestCrc32IsoHdlcEncodes() {
	VUB::Crc<Checksum::Crc32IsoHdlc> transform;
	VUB::Meta meta{};
	std::array<uint8, 13> wire{};
	SIM_EXPECT(transform.Encode(meta, std::span<const uint8>(digits.data(), digits.size()), wire).IsOk());
	const std::array<uint8, 13> expected = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x26, 0x39, 0xF4, 0xCB};
	SIM_EXPECT(wire == expected);
}


// A CRC of 12 bits takes two bytes: the check value of CRC-12/UMTS is 0DAF, so low byte first the bytes are AF 0D, and high byte first
// they are 0D AF.
void TestTwelveBitCrcTakesTwoBytes() {
	VUB::Meta meta{};
	VUB::Crc<Checksum::Crc12Umts> lowFirst;
	std::array<uint8, 11> wire{};
	SIM_EXPECT(lowFirst.Encode(meta, std::span<const uint8>(digits.data(), digits.size()), wire).IsOk());
	const std::array<uint8, 11> lowExpected = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0xAF, 0x0D};
	SIM_EXPECT(wire == lowExpected);

	VUB::Crc<Checksum::Crc12Umts, VUB::CrcOrder::highByteFirst> highFirst;
	std::array<uint8, 11> highWire{};
	SIM_EXPECT(highFirst.Encode(meta, std::span<const uint8>(digits.data(), digits.size()), highWire).IsOk());
	const std::array<uint8, 11> highExpected = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x0D, 0xAF};
	SIM_EXPECT(highWire == highExpected);
}


// A CRC of 5 bits takes one byte: the check value of CRC-5/USB is 19.
void TestFiveBitCrcTakesOneByte() {
	VUB::Crc<Checksum::Crc5Usb> transform;
	VUB::Meta meta{};
	std::array<uint8, 10> wire{};
	SIM_EXPECT(transform.Encode(meta, std::span<const uint8>(digits.data(), digits.size()), wire).IsOk());
	const std::array<uint8, 10> expected = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x19};
	SIM_EXPECT(wire == expected);
	SIM_EXPECT(VUB::Crc<Checksum::Crc5Usb>::growth == 1);
}


// A CRC of 24 bits takes three bytes: the check value of CRC-24/OPENPGP is 21CF02, so the bytes are 02 CF 21, or 21 CF 02 high byte first.
void TestTwentyFourBitCrcTakesThreeBytes() {
	VUB::Meta meta{};
	VUB::Crc<Checksum::Crc24OpenPgp> lowFirst;
	std::array<uint8, 12> wire{};
	SIM_EXPECT(lowFirst.Encode(meta, std::span<const uint8>(digits.data(), digits.size()), wire).IsOk());
	const std::array<uint8, 12> lowExpected = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x02, 0xCF, 0x21};
	SIM_EXPECT(wire == lowExpected);

	VUB::Crc<Checksum::Crc24OpenPgp, VUB::CrcOrder::highByteFirst> highFirst;
	std::array<uint8, 12> highWire{};
	SIM_EXPECT(highFirst.Encode(meta, std::span<const uint8>(digits.data(), digits.size()), highWire).IsOk());
	const std::array<uint8, 12> highExpected = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x21, 0xCF, 0x02};
	SIM_EXPECT(highWire == highExpected);
}


// A CRC of 64 bits takes eight bytes: the check value of CRC-64/ECMA-182 is 6C40DF5F0B497347, low byte first from 47 to 6C.
void TestSixtyFourBitCrcTakesEightBytes() {
	VUB::Crc<Checksum::Crc64Ecma182> transform;
	VUB::Meta meta{};
	std::array<uint8, 17> wire{};
	SIM_EXPECT(transform.Encode(meta, std::span<const uint8>(digits.data(), digits.size()), wire).IsOk());
	const std::array<uint8, 17> expected = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x47, 0x73, 0x49, 0x0B, 0x5F, 0xDF, 0x40, 0x6C};
	SIM_EXPECT(wire == expected);
	SIM_EXPECT(VUB::Crc<Checksum::Crc64Ecma182>::growth == 8);
	SIM_EXPECT(Checksum::Crc64Ecma182::Calculate(digits) == 0x6C40DF5F0B497347);
	SIM_EXPECT(transform.Decode(meta, std::span<uint8>(wire.data(), wire.size())).IsOk());
}


// Decode of the wire bytes by hand gives the digits back, in place, and the length of the body.
void TestModbusDecodesWireBytes() {
	VUB::Crc<Checksum::Crc16Modbus> transform;
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
	VUB::Crc<Checksum::Crc16Modbus, VUB::CrcOrder::highByteFirst> transform;
	VUB::Meta meta{};
	std::array<uint8, 11> wire = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x37, 0x4B};
	Result<std::size_t> decoded = transform.Decode(meta, std::span<uint8>(wire.data(), wire.size()));
	SIM_EXPECT(decoded.IsErr());
	SIM_EXPECT(decoded.Error() == ResultStatus::crcError);
}


// One changed payload byte is a crcError, and a packet shorter than the CRC is an invalid data error.
void TestDamagedPacketsAreRefused() {
	VUB::Crc<Checksum::Crc16Modbus> transform;
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


// The contract passes for the presets with the bitwise engine (the default), and for one of them with the table engine.
void TestPresetsPassContract() {
	VUB::Crc<Checksum::Crc16Modbus> modbus;
	SIM_EXPECT(VUB::CheckTransform(modbus, 7, 500, 40));
	VUB::Crc<Checksum::Crc16Modbus::With<Checksum::CrcTable>> modbusTable;
	SIM_EXPECT(VUB::CheckTransform(modbusTable, 8, 500, 40));
	VUB::Crc<Checksum::Crc8Smbus> smbus;
	SIM_EXPECT(VUB::CheckTransform(smbus, 9, 500, 40));
	VUB::Crc<Checksum::Crc32IsoHdlc> isoHdlc;
	SIM_EXPECT(VUB::CheckTransform(isoHdlc, 10, 500, 255));
	VUB::Crc<Checksum::Crc16Modbus, VUB::CrcOrder::highByteFirst> modbusHighFirst;
	SIM_EXPECT(VUB::CheckTransform(modbusHighFirst, 11, 500, 40));
	VUB::Crc<Checksum::Crc16Riello> riello;
	SIM_EXPECT(VUB::CheckTransform(riello, 12, 500, 40));
}


// The custom set, high byte first: 45 74 after the digits. The bytes come from the reference's CRC 4574.
void TestCustomHighByteFirstEncodes() {
	VUB::Crc<Checksum::Crc<customParameters>, VUB::CrcOrder::highByteFirst> transform;
	VUB::Meta meta{};
	std::array<uint8, 11> wire{};
	SIM_EXPECT(transform.Encode(meta, std::span<const uint8>(digits.data(), digits.size()), wire).IsOk());
	const std::array<uint8, 11> expected = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x45, 0x74};
	SIM_EXPECT(wire == expected);
}


// The custom set, low byte first, encoded and decoded by hand: the digits, then 4574 low byte first (74 45).
void TestCustomTransformEncodesAndDecodes() {
	VUB::Crc<Checksum::Crc<customParameters>> transform;
	VUB::Meta meta{};
	std::array<uint8, 11> wire{};
	Result<std::size_t> encoded = transform.Encode(meta, std::span<const uint8>(digits.data(), digits.size()), wire);
	SIM_EXPECT(encoded.IsOk());
	const std::array<uint8, 11> expected = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x74, 0x45};
	SIM_EXPECT(wire == expected);
	Result<std::size_t> decoded = transform.Decode(meta, std::span<uint8>(wire.data(), wire.size()));
	SIM_EXPECT(decoded.IsOk());
	SIM_EXPECT(decoded.Value() == 9);
}


// A transform holds no state: the CRC type and the byte order are template arguments, so no CRC transform has a member. Each is empty.
void TestTransformsHoldNoState() {
	SIM_EXPECT(std::is_empty_v<VUB::Crc<Checksum::Crc16Modbus>>);
	SIM_EXPECT((std::is_empty_v<VUB::Crc<Checksum::Crc16Modbus, VUB::CrcOrder::highByteFirst>>));
	SIM_EXPECT((std::is_empty_v<VUB::Crc<Checksum::Crc16Modbus::With<Checksum::CrcTable>>>));
	SIM_EXPECT(std::is_empty_v<VUB::Crc<Checksum::Crc<customParameters>>>);
	SIM_EXPECT(std::is_empty_v<VUB::Crc<Checksum::Crc8Smbus>>);
}


// The byte order is part of the wire, so it is part of the fingerprint: the two orders of one CRC differ, and so do two CRCs of one order.
void TestFingerprintsDifferByParametersAndOrder() {
	SIM_EXPECT((VUB::Crc<Checksum::Crc16Modbus>::fingerprint != VUB::Crc<Checksum::Crc16Modbus, VUB::CrcOrder::highByteFirst>::fingerprint));
	SIM_EXPECT((VUB::Crc<Checksum::Crc16Modbus>::fingerprint != VUB::Crc<Checksum::Crc16Usb>::fingerprint));
	SIM_EXPECT((VUB::Crc<Checksum::Crc16Tms37157>::fingerprint != VUB::Crc<Checksum::Crc16Riello>::fingerprint));
	SIM_EXPECT((VUB::Crc<Checksum::Crc<customParameters>>::fingerprint != VUB::Crc<Checksum::Crc<customParameters>, VUB::CrcOrder::highByteFirst>::fingerprint));
	SIM_EXPECT((VUB::Crc<Checksum::Crc16Modbus, VUB::CrcOrder::highByteFirst>::fingerprint != VUB::Crc<Checksum::Crc16Usb, VUB::CrcOrder::highByteFirst>::fingerprint));
}


// A stack over the faulty loopback with flag framing: every payload the handler gets is one that was sent, whole, and the lost or
// changed frames are counted. A corrupted frame that the framing lets through is a transformError, and none reaches the handler.
using CrcStreamEnd = VUB::StreamEnd<64, 512>;


using CrcStream = VUB::Stack<+[] {
	return VUB::Builder()
		.CountDrops()
		.Transform<VUB::Crc<Checksum::Crc16Modbus>>()
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
		.CountDrops()
		.Transform<VUB::Crc<Checksum::Crc16Modbus>>()
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
		.CountDrops()
		.Transform<VUB::Crc<Checksum::Crc16Modbus, VUB::CrcOrder::highByteFirst>>()
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
	TestTwelveBitCrcTakesTwoBytes();
	TestFiveBitCrcTakesOneByte();
	TestTwentyFourBitCrcTakesThreeBytes();
	TestSixtyFourBitCrcTakesEightBytes();
	TestModbusDecodesWireBytes();
	TestModbusByteOrderIsChecked();
	TestDamagedPacketsAreRefused();
	TestPresetsPassContract();
	TestFaultyLoopbackNeverDeliversCorruptedPayloads();
	TestPacketFaultsCountTransformErrors();
	TestWrongCrcIsCountedAsTransformError();
	TestChainByteOrderReachesTheWire();
	TestCustomHighByteFirstEncodes();
	TestCustomTransformEncodesAndDecodes();
	TestTransformsHoldNoState();
	TestFingerprintsDifferByParametersAndOrder();
	return SimFinish("TestVubCrcWire");
}
