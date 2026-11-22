#include "SimCheck.h"
#include <Drivers/Interface/Modbus/ModbusServer.h>
#include <Drivers/Interface/Modbus/RTU/RtuFramer.h>
#include <Utilities/Data/RegisterMap/RegisterView.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <cstddef>
#include <cstdio>
#include <random>
#include <span>
#include <vector>


// Fuzz: 100 000 random frames go into a server, a mix of well-formed requests with a valid CRC and random bytes. Each frame must be
// answered or dropped without an abort, and every reply must be a valid frame from the server's address. After each frame the three
// variables of the view must hold what the frame allows: the values of a write the server accepted, or the values from before. A
// broadcast write gets no reply, so either outcome is allowed there. Memory outside the view is checked by the sanitizers, see run.sh.


constexpr int FuzzFrames = 100000;
constexpr uint16 SetpointLimit = 100;
constexpr uint8 OurAddress = 1;
constexpr uint32 SetpointAddress = 0x0000;
constexpr uint32 CounterAddress = 0x0002;
constexpr uint32 RelayAddress = 0x0000;


uint16 setpoint = 0;
uint16 counter = 0;
bool relay = false;


bool CheckSetpoint(uint16 value) {
	return value <= SetpointLimit;
}


uint16 ReadChip() {
	return 0x1234;
}


using FuzzView = RegisterView<+[] {
	return RegisterViewBuilder()
		.HoldingRegisters(0x0000, Variable(&setpoint, &CheckSetpoint))
		.HoldingRegisters(0x0002, Variable(&counter))
		.InputRegisters(0x0010, Computed(&ReadChip))
		.Coils(0x0000, Variable(&relay));
}>;


// The three variables of the view, as one value.
struct Values {
	uint16 setpoint;
	uint16 counter;
	bool relay;
};


// What one write frame would store in the entries it covers. A frame that is not a consistent write has no covered entries.
struct Expect {
	bool setpointCovered = false;
	uint16 setpointValue = 0;
	bool counterCovered = false;
	uint16 counterValue = 0;
	bool relayCovered = false;
	bool relayValue = false;
};


// A frame the server may take: its address, its function code, and for a consistent write its range and where its data starts.
struct Parsed {
	bool crcValid = false;
	uint8 address = 0;
	uint8 code = 0;
	bool write = false;
	uint32 first = 0;
	uint32 quantity = 0;
	std::size_t dataStart = 0;
};


// The feed: each Receive gives the next random frame. Frames longer than the buffer are reported as invalidData, as the transport does.
class FuzzLink {
private:
	std::mt19937 generator;
	std::vector<uint8> pending;
	std::vector<uint8> taken;
	std::vector<uint8> reply;
	std::size_t replies = 0;
	std::size_t invalidReplies = 0;


public:
	explicit FuzzLink(uint32 seed) : generator(seed) {
	}


	ResultStatus Begin() {
		return ResultStatus::ok;
	}


	// A reply must be a valid frame from our address. The server sends one reply per request it answers.
	ResultStatus Send(std::span<const uint8> frame, uint32) {
		replies++;
		reply.assign(frame.begin(), frame.end());
		if (RtuFramer::Check(frame, OurAddress) != ResultStatus::ok) {
			invalidReplies++;
		}
		return ResultStatus::ok;
	}


	Result<uint16> Receive(std::span<uint8> frame, uint32) {
		pending = RandomFrame();
		taken.clear();
		if (pending.size() > frame.size()) {
			return ResultStatus::invalidData;
		}

		for (std::size_t index = 0; index < pending.size(); index++) {
			frame[index] = pending[index];
		}
		taken = pending;
		return static_cast<uint16>(pending.size());
	}


	// The frame the server took in the last Process call, empty when none was delivered.
	const std::vector<uint8>& TakenFrame() const {
		return taken;
	}


	const std::vector<uint8>& LastReply() const {
		return reply;
	}


	std::size_t Replies() const {
		return replies;
	}


	std::size_t InvalidReplies() const {
		return invalidReplies;
	}


	// A random frame: half are a request with a valid CRC, the rest are random bytes of any length.
	std::vector<uint8> RandomFrame() {
		std::uniform_int_distribution<int> kind(0, 9);
		if (kind(generator) >= 5) {
			std::uniform_int_distribution<int> length(0, 300);
			std::vector<uint8> bytes(static_cast<std::size_t>(length(generator)));
			for (uint8& byte : bytes) {
				byte = static_cast<uint8>(generator());
			}
			return bytes;
		}

		static const uint8 codes[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x0F, 0x10, 0x07, 0x83};
		std::uniform_int_distribution<int> codePick(0, 9);
		std::uniform_int_distribution<int> addressPick(0, 2);
		std::uniform_int_distribution<int> quantityPick(0, 140);
		std::uniform_int_distribution<int> addressValue(0, 0xFFFF);
		std::uniform_int_distribution<int> byteValue(0, 255);

		uint8 code = codes[codePick(generator)];
		std::vector<uint8> body;
		body.push_back(static_cast<uint8>(addressPick(generator)));
		body.push_back(code);

		// Most addresses are small, so the requests land in the view; some are anywhere in the 16-bit range.
		bool wide = codePick(generator) == 0;
		uint16 address = static_cast<uint16>(wide ? addressValue(generator) : addressPick(generator));
		uint16 quantity = static_cast<uint16>(quantityPick(generator));
		body.push_back(static_cast<uint8>(address >> 8));
		body.push_back(static_cast<uint8>(address & 0xFF));

		if (code == 0x0F || code == 0x10) {
			body.push_back(static_cast<uint8>(quantity >> 8));
			body.push_back(static_cast<uint8>(quantity & 0xFF));
			std::size_t byteCount = code == 0x10 ? 2 * static_cast<std::size_t>(quantity) : (static_cast<std::size_t>(quantity) + 7) / 8;
			if (byteValue(generator) % 8 == 0) {
				byteCount = static_cast<std::size_t>(byteValue(generator));
			}
			body.push_back(static_cast<uint8>(byteCount));
			for (std::size_t index = 0; index < byteCount && index < 260; index++) {
				body.push_back(static_cast<uint8>(byteValue(generator)));
			}
		} else {
			body.push_back(static_cast<uint8>(quantity >> 8));
			body.push_back(static_cast<uint8>(quantity & 0xFF));
		}

		uint16 crc = RtuFramer::Crc(body);
		body.push_back(static_cast<uint8>(crc & 0xFF));
		body.push_back(static_cast<uint8>(crc >> 8));
		return body;
	}
};


Values TakeValues() {
	return Values{setpoint, counter, relay};
}


bool Same(const Values& left, const Values& right) {
	return left.setpoint == right.setpoint && left.counter == right.counter && left.relay == right.relay;
}


// The values after a write that stores e: the covered entries take their new values, the others keep theirs.
Values Apply(Values before, const Expect& e) {
	if (e.setpointCovered) {
		before.setpoint = e.setpointValue;
	}

	if (e.counterCovered) {
		before.counter = e.counterValue;
	}

	if (e.relayCovered) {
		before.relay = e.relayValue;
	}

	return before;
}


bool IsWriteCode(uint8 code) {
	return code == 0x05 || code == 0x06 || code == 0x0F || code == 0x10;
}


// Reads the frame's header. A frame is a consistent write only when its length and byte count agree with its quantity, and a
// single coil value is FF00 or 0000. Anything else is not a write, so the oracle expects no change from it.
Parsed ParseFrame(const std::vector<uint8>& frame) {
	Parsed parsed;
	if (frame.size() < 4 || RtuFramer::Check(frame, frame[0]) != ResultStatus::ok) {
		return parsed;
	}

	parsed.crcValid = true;
	parsed.address = frame[0];
	parsed.code = frame[1];
	std::size_t size = frame.size();

	if ((parsed.code == 0x05 || parsed.code == 0x06) && size == 8) {
		parsed.first = static_cast<uint32>((frame[2] << 8) | frame[3]);
		parsed.quantity = 1;
		parsed.dataStart = 4;
		if (parsed.code == 0x06) {
			parsed.write = true;
		} else {
			parsed.write = (frame[4] == 0xFF && frame[5] == 0x00) || (frame[4] == 0x00 && frame[5] == 0x00);
		}
	} else if ((parsed.code == 0x0F || parsed.code == 0x10) && size >= 9) {
		parsed.first = static_cast<uint32>((frame[2] << 8) | frame[3]);
		parsed.quantity = static_cast<uint32>((frame[4] << 8) | frame[5]);
		parsed.dataStart = 7;
		std::size_t byteCount = frame[6];
		std::size_t dataBytes = size - 9;
		if (parsed.code == 0x10) {
			parsed.write = parsed.quantity >= 1 && parsed.quantity <= 123 && byteCount == 2 * parsed.quantity && dataBytes == byteCount;
		} else {
			parsed.write = parsed.quantity >= 1 && parsed.quantity <= 1968 && byteCount == (parsed.quantity + 7) / 8 && dataBytes == byteCount;
		}
	}

	return parsed;
}


bool Covers(const Parsed& parsed, uint32 entry) {
	return parsed.write && parsed.first <= entry && entry < parsed.first + parsed.quantity;
}


uint16 WordAt(const std::vector<uint8>& frame, const Parsed& parsed, uint32 entry) {
	std::size_t offset = parsed.dataStart + 2 * static_cast<std::size_t>(entry - parsed.first);
	return static_cast<uint16>((frame[offset] << 8) | frame[offset + 1]);
}


bool BitAt(const std::vector<uint8>& frame, const Parsed& parsed, uint32 entry) {
	if (parsed.code == 0x05) {
		return frame[4] == 0xFF;
	}

	std::size_t index = static_cast<std::size_t>(entry - parsed.first);
	return ((frame[parsed.dataStart + index / 8] >> (index % 8)) & 1) != 0;
}


Expect ExpectOf(const Parsed& parsed, const std::vector<uint8>& frame) {
	Expect expect;
	if (!parsed.write) {
		return expect;
	}

	bool holding = parsed.code == 0x06 || parsed.code == 0x10;
	bool coil = parsed.code == 0x05 || parsed.code == 0x0F;
	if (holding && Covers(parsed, SetpointAddress)) {
		expect.setpointCovered = true;
		expect.setpointValue = WordAt(frame, parsed, SetpointAddress);
	}

	if (holding && Covers(parsed, CounterAddress)) {
		expect.counterCovered = true;
		expect.counterValue = WordAt(frame, parsed, CounterAddress);
	}

	if (coil && Covers(parsed, RelayAddress)) {
		expect.relayCovered = true;
		expect.relayValue = BitAt(frame, parsed, RelayAddress);
	}

	return expect;
}


int main() {
	FuzzLink link(20261122);
	ModbusServer<FuzzLink, RtuFramer, FuzzView, +[] {
		return ServerBuilder()
			.SlaveAddress(OurAddress);
	}> server(link);

	SIM_EXPECT(link.Begin() == ResultStatus::ok);

	std::size_t acceptedWrites = 0;
	std::size_t refusedWrites = 0;
	std::size_t dropped = 0;
	for (int frame = 0; frame < FuzzFrames; frame++) {
		Values before = TakeValues();
		std::size_t repliesBefore = link.Replies();
		ResultStatus status = server.Process();
		Values after = TakeValues();

		const std::vector<uint8>& taken = link.TakenFrame();
		Parsed parsed = ParseFrame(taken);
		Values expected = Apply(before, ExpectOf(parsed, taken));

		bool replied = link.Replies() > repliesBefore;
		// A reply is the address, the function code and the data. An exception sets the top bit of the function code.
		bool refused = replied && link.LastReply().size() >= 2 && (link.LastReply()[1] & 0x80) != 0;
		bool ours = parsed.crcValid && parsed.address == OurAddress;
		bool broadcast = parsed.crcValid && parsed.address == 0;

		if (ours) {
			bool accepted = replied && !refused;
			SIM_EXPECT(Same(after, accepted ? expected : before));
			if (accepted && parsed.write) {
				acceptedWrites++;
			}

			if (refused && parsed.write) {
				refusedWrites++;
			}
		} else if (broadcast) {
			SIM_EXPECT(Same(after, expected) || Same(after, before));
		} else {
			SIM_EXPECT(Same(after, before));
		}

		SIM_EXPECT(setpoint <= SetpointLimit);
		if (status != ResultStatus::ok) {
			dropped++;
		}
	}

	SIM_EXPECT(link.InvalidReplies() == 0);
	SIM_EXPECT(link.Replies() > 0);
	SIM_EXPECT(acceptedWrites > 0);
	SIM_EXPECT(refusedWrites > 0);
	std::printf("frames: %d, replies sent: %zu, accepted writes: %zu, refused writes: %zu, frames dropped with a status: %zu\n",
		FuzzFrames, link.Replies(), acceptedWrites, refusedWrites, dropped);
	return SimFinish("TestModbusFuzz");
}
