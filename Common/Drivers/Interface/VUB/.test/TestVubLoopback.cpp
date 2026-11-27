#include "SimCheck.h"
#include "Chains.h"
#include <Drivers/Interface/VUB/TestKit/Loopback.h>
#include <Drivers/Interface/VUB/TestKit/Wire.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <span>


std::array<uint8, 64> Sequence(std::size_t count) {
	std::array<uint8, 64> bytes{};
	for (std::size_t index = 0; index < count && index < bytes.size(); index++) {
		bytes[index] = static_cast<uint8>(index);
	}

	return bytes;
}


// Without faults every byte arrives once, in order.
void TestCleanStream() {
	VUB::Faults faults;
	VUB::StreamWire<128> wire(faults, 1);
	std::array<uint8, 64> bytes = Sequence(20);
	wire.Push(std::span<const uint8>(bytes.data(), 20));
	RecordSink sink;
	wire.Deliver(sink);
	SIM_EXPECT(sink.byteCount == 20);
	for (std::size_t index = 0; index < 20; index++) {
		SIM_EXPECT(sink.bytes[index] == index);
	}
}


// Every byte dropped: nothing arrives, and every drop is counted.
void TestDropAll() {
	VUB::Faults faults;
	faults.dropPercent = 100;
	VUB::StreamWire<128> wire(faults, 1);
	std::array<uint8, 30> bytes{};
	wire.Push(std::span<const uint8>(bytes.data(), 30));
	RecordSink sink;
	wire.Deliver(sink);
	SIM_EXPECT(sink.byteCount == 0);
	SIM_EXPECT(wire.Counts().dropped == 30);
}


// Every byte duplicated: each arrives twice.
void TestDuplicateAll() {
	VUB::Faults faults;
	faults.duplicatePercent = 100;
	VUB::StreamWire<128> wire(faults, 1);
	std::array<uint8, 10> bytes{};
	wire.Push(std::span<const uint8>(bytes.data(), 10));
	RecordSink sink;
	wire.Deliver(sink);
	SIM_EXPECT(sink.byteCount == 20);
	SIM_EXPECT(wire.Counts().duplicated == 10);
}


// Every byte corrupted: each arrives with exactly one bit different from the byte sent.
void TestCorruptAll() {
	VUB::Faults faults;
	faults.corruptPercent = 100;
	faults.seed = 42;
	VUB::StreamWire<128> wire(faults, 42);
	std::array<uint8, 64> bytes = Sequence(20);
	wire.Push(std::span<const uint8>(bytes.data(), 20));
	RecordSink sink;
	wire.Deliver(sink);
	SIM_EXPECT(sink.byteCount == 20);
	bool oneBit = true;
	for (std::size_t index = 0; index < 20; index++) {
		uint8 difference = static_cast<uint8>(sink.bytes[index] ^ bytes[index]);
		oneBit = oneBit && difference != 0 && (difference & (difference - 1)) == 0;
	}

	SIM_EXPECT(oneBit);
	SIM_EXPECT(wire.Counts().corrupted == 20);
}


// With delays, later bytes can arrive first: the wire reorders. Nothing is lost, and every byte arrives once the time has come.
void TestDelayReorders() {
	VUB::Faults faults;
	faults.maxDelayUs = 50;
	faults.seed = 9;
	VUB::StreamWire<128> wire(faults, 9);
	std::array<uint8, 64> bytes = Sequence(64);
	wire.SetNow(0);
	wire.Push(std::span<const uint8>(bytes.data(), 64));
	RecordSink early;
	wire.Deliver(early);
	wire.SetNow(100);
	RecordSink late;
	wire.Deliver(late);
	SIM_EXPECT(early.byteCount + late.byteCount == 64);

	// The order of arrival is the early bytes, then the late ones. A byte sent before another can arrive after it: an inversion.
	std::array<uint8, 64> arrival{};
	std::size_t total = 0;
	for (std::size_t index = 0; index < early.byteCount; index++) {
		arrival[total] = early.bytes[index];
		total++;
	}

	for (std::size_t index = 0; index < late.byteCount; index++) {
		arrival[total] = late.bytes[index];
		total++;
	}

	bool reordered = false;
	for (std::size_t first = 0; first < total; first++) {
		for (std::size_t second = first + 1; second < total; second++) {
			reordered = reordered || arrival[first] > arrival[second];
		}
	}

	SIM_EXPECT(reordered);
}


// Packets pushed at time 100 with a delay of up to 50 microseconds: none arrives before its due time (nothing is due at 99), not
// all have arrived at 100, some but not all by 125, and after 150 every packet has arrived whole. So the delay applies and is
// spread over its range.
void TestPacketDelayed() {
	VUB::Faults faults;
	faults.maxDelayUs = 50;
	faults.seed = 5;
	VUB::PacketWire<32, 32> wire(faults, 5);
	wire.SetNow(100);
	for (std::size_t index = 0; index < 32; index++) {
		std::array<uint8, 2> packet = {static_cast<uint8>(index), 0x55};
		wire.Push(std::span<const uint8>(packet.data(), packet.size()));
	}

	RecordSink early;
	wire.SetNow(99);
	wire.Deliver(early);
	SIM_EXPECT(early.packetCount == 0);

	wire.SetNow(100);
	RecordSink atPush;
	wire.Deliver(atPush);
	SIM_EXPECT(atPush.packetCount < 32);

	wire.SetNow(125);
	RecordSink middle;
	wire.Deliver(middle);
	SIM_EXPECT(atPush.packetCount + middle.packetCount > 0);
	SIM_EXPECT(atPush.packetCount + middle.packetCount < 32);

	wire.SetNow(150);
	RecordSink later;
	wire.Deliver(later);
	SIM_EXPECT(atPush.packetCount + middle.packetCount + later.packetCount == 32);
	for (std::size_t index = 0; index < later.packetCount && index < later.packetLength.size(); index++) {
		SIM_EXPECT(later.packetLength[index] == 2);
	}
}


// Every packet dropped: nothing arrives, and every drop is counted.
void TestPacketDropAll() {
	VUB::Faults faults;
	faults.dropPercent = 100;
	VUB::PacketWire<16, 32> wire(faults, 1);
	std::array<uint8, 3> packet = {1, 2, 3};
	for (std::size_t index = 0; index < 10; index++) {
		wire.Push(std::span<const uint8>(packet.data(), packet.size()));
	}

	RecordSink sink;
	wire.Deliver(sink);
	SIM_EXPECT(sink.packetCount == 0);
	SIM_EXPECT(wire.Counts().dropped == 10);
}


// Every packet duplicated: each arrives twice, in the order sent, so the lengths come out in pairs.
void TestPacketDuplicateAll() {
	VUB::Faults faults;
	faults.duplicatePercent = 100;
	VUB::PacketWire<16, 32> wire(faults, 1);
	for (std::size_t index = 0; index < 5; index++) {
		std::array<uint8, 8> packet{};
		wire.Push(std::span<const uint8>(packet.data(), index + 1));
	}

	RecordSink sink;
	wire.Deliver(sink);
	SIM_EXPECT(sink.packetCount == 10);
	SIM_EXPECT(wire.Counts().duplicated == 5);
	for (std::size_t index = 0; index < 5; index++) {
		SIM_EXPECT(sink.packetLength[2 * index] == index + 1);
		SIM_EXPECT(sink.packetLength[2 * index + 1] == index + 1);
	}
}


// A packet longer than the line's MaxPacket is refused by Send with an error, and the wire carries nothing. A packet of exactly
// MaxPacket bytes goes through whole.
void TestPacketSendRefusesTooLong() {
	VUB::PacketLine<64, 16> line(VUB::Faults{});
	std::array<uint8, 100> big{};
	SIM_EXPECT(line.a.Send(std::span<const uint8>(big.data(), big.size())) == ResultStatus::messageTooLong);
	SIM_EXPECT(line.toB.Counts().pushed == 0);

	std::array<uint8, 64> exact{};
	exact[63] = 0x7A;
	SIM_EXPECT(line.a.Send(std::span<const uint8>(exact.data(), exact.size())) == ResultStatus::ok);
	RecordSink sink;
	line.DeliverToB(sink);
	SIM_EXPECT(sink.packetCount == 1);
	SIM_EXPECT(sink.packetLength[0] == 64);
}


// Rates. The fraction of an event out of a total, in tenths of a percent, is within 1.5 points of the configured percent.
bool NearPercent(uint32 observed, uint32 total, uint32 percent) {
	uint64 scaled = static_cast<uint64>(observed) * 1000 / total;
	uint64 target = static_cast<uint64>(percent) * 10;
	uint64 difference = scaled > target ? scaled - target : target - scaled;
	return difference <= 15;
}


// Over 20000 bytes, each fault at its own configured rate (10, 20 and 15 percent): the byte wire drops, corrupts and duplicates close
// to those rates.
void TestByteWireRates() {
	const uint32 total = 20000;
	std::array<uint8, 1> one{};

	VUB::Faults dropFaults;
	dropFaults.dropPercent = 10;
	dropFaults.seed = 3;
	VUB::StreamWire<128> dropWire(dropFaults, 3);
	RecordSink dropSink;
	for (uint32 index = 0; index < total; index++) {
		dropWire.Push(std::span<const uint8>(one.data(), 1));
		dropWire.Deliver(dropSink);
	}

	SIM_EXPECT(dropWire.Counts().pushed == total);
	SIM_EXPECT(NearPercent(dropWire.Counts().dropped, total, 10));

	VUB::Faults corruptFaults;
	corruptFaults.corruptPercent = 20;
	corruptFaults.seed = 4;
	VUB::StreamWire<128> corruptWire(corruptFaults, 4);
	RecordSink corruptSink;
	for (uint32 index = 0; index < total; index++) {
		corruptWire.Push(std::span<const uint8>(one.data(), 1));
		corruptWire.Deliver(corruptSink);
	}

	SIM_EXPECT(NearPercent(corruptWire.Counts().corrupted, total, 20));

	VUB::Faults duplicateFaults;
	duplicateFaults.duplicatePercent = 15;
	duplicateFaults.seed = 5;
	VUB::StreamWire<128> duplicateWire(duplicateFaults, 5);
	RecordSink duplicateSink;
	for (uint32 index = 0; index < total; index++) {
		duplicateWire.Push(std::span<const uint8>(one.data(), 1));
		duplicateWire.Deliver(duplicateSink);
	}

	SIM_EXPECT(NearPercent(duplicateWire.Counts().duplicated, total, 15));
}


// The same rates over 10000 packets of 4 bytes on the packet wire.
void TestPacketWireRates() {
	const uint32 total = 10000;
	std::array<uint8, 4> packet = {1, 2, 3, 4};

	VUB::Faults dropFaults;
	dropFaults.dropPercent = 10;
	dropFaults.seed = 3;
	VUB::PacketWire<128, 8> dropWire(dropFaults, 3);
	RecordSink dropSink;
	for (uint32 index = 0; index < total; index++) {
		dropWire.Push(std::span<const uint8>(packet.data(), packet.size()));
		dropWire.Deliver(dropSink);
	}

	SIM_EXPECT(dropWire.Counts().pushed == total);
	SIM_EXPECT(NearPercent(dropWire.Counts().dropped, total, 10));

	VUB::Faults corruptFaults;
	corruptFaults.corruptPercent = 20;
	corruptFaults.seed = 4;
	VUB::PacketWire<128, 8> corruptWire(corruptFaults, 4);
	RecordSink corruptSink;
	for (uint32 index = 0; index < total; index++) {
		corruptWire.Push(std::span<const uint8>(packet.data(), packet.size()));
		corruptWire.Deliver(corruptSink);
	}

	SIM_EXPECT(NearPercent(corruptWire.Counts().corrupted, total, 20));

	VUB::Faults duplicateFaults;
	duplicateFaults.duplicatePercent = 15;
	duplicateFaults.seed = 5;
	VUB::PacketWire<128, 8> duplicateWire(duplicateFaults, 5);
	RecordSink duplicateSink;
	for (uint32 index = 0; index < total; index++) {
		duplicateWire.Push(std::span<const uint8>(packet.data(), packet.size()));
		duplicateWire.Deliver(duplicateSink);
	}

	SIM_EXPECT(NearPercent(duplicateWire.Counts().duplicated, total, 15));
}


// What a sink was given: the count of bytes and packets, and a hash of their values.
struct DigestSink {
	uint32 digest = 2166136261u;
	uint32 count = 0;


	void Feed(uint8 byte) {
		Mix(byte);
		count++;
	}


	void FeedPacket(std::span<const uint8> packet) {
		Mix(static_cast<uint8>(packet.size()));
		for (uint8 value : packet) {
			Mix(value);
		}

		count++;
	}


	void Mix(uint8 value) {
		digest = (digest ^ value) * 16777619u;
	}
};


// A byte wire run of 400 steps, each with a byte and the faults of the seed, then delivered.
DigestSink RunByteWire(uint32 seed) {
	VUB::Faults faults;
	faults.dropPercent = 10;
	faults.corruptPercent = 10;
	faults.duplicatePercent = 10;
	faults.maxDelayUs = 30;
	faults.seed = seed;
	VUB::StreamWire<256> wire(faults, seed);
	DigestSink sink;
	for (uint32 now = 0; now < 400; now++) {
		wire.SetNow(now);
		std::array<uint8, 1> byte = {static_cast<uint8>(now)};
		wire.Push(std::span<const uint8>(byte.data(), 1));
		wire.Deliver(sink);
	}

	wire.SetNow(1000);
	wire.Deliver(sink);
	return sink;
}


// A packet wire run of 400 steps, each with a packet and the faults of the seed, then delivered.
DigestSink RunPacketWire(uint32 seed) {
	VUB::Faults faults;
	faults.dropPercent = 10;
	faults.corruptPercent = 10;
	faults.duplicatePercent = 10;
	faults.maxDelayUs = 30;
	faults.seed = seed;
	VUB::PacketWire<256, 8> wire(faults, seed);
	DigestSink sink;
	for (uint32 now = 0; now < 400; now++) {
		wire.SetNow(now);
		std::array<uint8, 3> packet = {static_cast<uint8>(now), 0x21, 0x42};
		wire.Push(std::span<const uint8>(packet.data(), packet.size()));
		wire.Deliver(sink);
	}

	wire.SetNow(1000);
	wire.Deliver(sink);
	return sink;
}


// The same seed and faults give the same output on each wire, and another seed gives another output.
void TestWireRepeatsBySeed() {
	DigestSink byteA = RunByteWire(7);
	DigestSink byteB = RunByteWire(7);
	DigestSink byteOther = RunByteWire(8);
	SIM_EXPECT(byteA.count == byteB.count);
	SIM_EXPECT(byteA.digest == byteB.digest);
	SIM_EXPECT(byteA.digest != byteOther.digest);

	DigestSink packetA = RunPacketWire(7);
	DigestSink packetB = RunPacketWire(7);
	DigestSink packetOther = RunPacketWire(8);
	SIM_EXPECT(packetA.count == packetB.count);
	SIM_EXPECT(packetA.digest == packetB.digest);
	SIM_EXPECT(packetA.digest != packetOther.digest);
}


// A stream end accepts a round budget of bytes and reports how many it took; the rest stays with the caller.
void TestStreamEndBudget() {
	VUB::Faults faults;
	VUB::StreamLine<64, 128> line(faults);
	line.a.StartRound(5);
	std::array<uint8, 64> bytes = Sequence(9);
	Result<std::size_t> first = line.a.Write(std::span<const uint8>(bytes.data(), 9));
	SIM_EXPECT(first.IsOk());
	SIM_EXPECT(first.Value() == 5);
	Result<std::size_t> second = line.a.Write(std::span<const uint8>(bytes.data() + 5, 4));
	SIM_EXPECT(second.IsOk());
	SIM_EXPECT(second.Value() == 0);
}


// A packet end takes one packet per budget unit and returns busy after that.
void TestPacketEndBudget() {
	VUB::Faults faults;
	VUB::PacketLine<64, 16> line(faults);
	line.a.StartRound(1);
	std::array<uint8, 3> packet = {1, 2, 3};
	SIM_EXPECT(line.a.Send(std::span<const uint8>(packet.data(), packet.size())) == ResultStatus::ok);
	SIM_EXPECT(line.a.Send(std::span<const uint8>(packet.data(), packet.size())) == ResultStatus::busy);
}


// A sink that keeps the first byte of each packet it is given, in the order of delivery.
struct FirstByteSink {
	std::array<uint8, 64> firsts{};
	std::size_t count = 0;


	void Feed(uint8) {
	}


	void FeedPacket(std::span<const uint8> packet) {
		if (count < firsts.size()) {
			firsts[count] = packet[0];
		}

		count++;
	}
};


// True when the first bytes of the sink are rising, which is the order the packets were pushed in.
bool FirstBytesRise(const FirstByteSink& sink) {
	for (std::size_t index = 1; index < sink.count && index < sink.firsts.size(); index++) {
		if (sink.firsts[index] <= sink.firsts[index - 1]) {
			return false;
		}
	}

	return true;
}


// With keepsOrder, 32 packets pushed one microsecond apart with delays up to 50 us come out in push order. The same line without it
// reorders them, so the flag is what keeps the order.
void TestPacketKeepsOrder() {
	VUB::Faults faults;
	faults.maxDelayUs = 50;
	faults.seed = 5;
	faults.keepsOrder = true;
	VUB::PacketWire<64, 32> wire(faults, 5);
	for (std::size_t index = 0; index < 32; index++) {
		wire.SetNow(100 + static_cast<uint32>(index));
		std::array<uint8, 2> packet = {static_cast<uint8>(index), 0x55};
		wire.Push(std::span<const uint8>(packet.data(), packet.size()));
	}

	FirstByteSink ordered;
	wire.SetNow(1000);
	wire.Deliver(ordered);
	SIM_EXPECT(ordered.count == 32);
	SIM_EXPECT(FirstBytesRise(ordered));

	VUB::Faults free = faults;
	free.keepsOrder = false;
	VUB::PacketWire<64, 32> loose(free, 5);
	for (std::size_t index = 0; index < 32; index++) {
		loose.SetNow(100 + static_cast<uint32>(index));
		std::array<uint8, 2> packet = {static_cast<uint8>(index), 0x55};
		loose.Push(std::span<const uint8>(packet.data(), packet.size()));
	}

	FirstByteSink reordered;
	loose.SetNow(1000);
	loose.Deliver(reordered);
	SIM_EXPECT(reordered.count == 32);
	SIM_EXPECT(!FirstBytesRise(reordered));
}


// Packets due just below the 32-bit wrap are due once the clock has wrapped: 32 packets pushed at 0xFFFFFFF0 with delays up to 30 us are all
// delivered at 0x100. Compared as plain integers, the ones due after the wrap would stay on the wire.
void TestPacketDueAcrossWrap() {
	VUB::Faults faults;
	faults.maxDelayUs = 30;
	faults.seed = 9;
	VUB::PacketWire<64, 32> wire(faults, 9);
	wire.SetNow(0xFFFFFFF0u);
	for (std::size_t index = 0; index < 32; index++) {
		std::array<uint8, 2> packet = {static_cast<uint8>(index), 0x55};
		wire.Push(std::span<const uint8>(packet.data(), packet.size()));
	}

	FirstByteSink sink;
	wire.SetNow(0x100u);
	wire.Deliver(sink);
	SIM_EXPECT(sink.count == 32);
}


int main() {
	TestPacketKeepsOrder();
	TestPacketDueAcrossWrap();
	TestCleanStream();
	TestDropAll();
	TestDuplicateAll();
	TestCorruptAll();
	TestDelayReorders();
	TestPacketDelayed();
	TestPacketDropAll();
	TestPacketDuplicateAll();
	TestPacketSendRefusesTooLong();
	TestByteWireRates();
	TestPacketWireRates();
	TestWireRepeatsBySeed();
	TestStreamEndBudget();
	TestPacketEndBudget();
	return SimFinish("TestVubLoopback");
}
