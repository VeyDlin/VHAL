#include "SimCheck.h"
#include <Drivers/Interface/VUB/Requests/Body.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>


// The codec of the requests layer, run on the bodies of the motor description and on a few bodies that exercise the other field types.
// Every expected byte string is written by hand from the wire layout of the bodies.


namespace Motor {

enum class MotorStatus : uint8 {
	started = 0,
	warming = 1,
	failed = 2
};

struct MotorStart {
	uint16 speedRpm = 0;
	uint8 direction = 0;
	uint16 rampMs = 300;
};

struct MotorReport {
	uint16 speedRpm = 0;
	uint16 currentMa = 0;
};

struct MotorFault {
	uint8 code = 0;
};

struct MotorHealth {
	uint32 uptimeS = 0;
	uint8 resetReason = 0;
	uint8 state = 0;
};

}


namespace Probe {

struct Flags {
	bool armed = false;
	bool fault = false;
};

struct Wide {
	int64 offset = 0;
	uint64 counter = 0;
	int8 trim = 0;
	int32 bias = 0;
};

struct Shape {
	Motor::MotorStatus status = Motor::MotorStatus::started;
	float gain = 0.0f;
};

struct Ladder {
	std::array<uint16, 3> steps{};
};

struct Outer {
	Motor::MotorReport inner{};
	uint8 tail = 0;
};

struct Fleet {
	std::array<Motor::MotorReport, 2> reports{};
};

struct Tail {
	uint8 head = 0;
	uint16 rest = 0;
	uint8 extra = 7;
	uint8 more = 9;
};

}


namespace VUB {

template<>
struct BodyOf<Motor::MotorStart> {
	static consteval auto Layout() {
		return Fields<Motor::MotorStart>()
			.Field<&Motor::MotorStart::speedRpm>()
			.Field<&Motor::MotorStart::direction>()
			.Optional<&Motor::MotorStart::rampMs>();
	}
};


template<>
struct BodyOf<Motor::MotorReport> {
	static consteval auto Layout() {
		return Fields<Motor::MotorReport>()
			.Field<&Motor::MotorReport::speedRpm>()
			.Field<&Motor::MotorReport::currentMa>();
	}
};


template<>
struct BodyOf<Motor::MotorFault> {
	static consteval auto Layout() {
		return Fields<Motor::MotorFault>()
			.Field<&Motor::MotorFault::code>();
	}
};


template<>
struct BodyOf<Motor::MotorHealth> {
	static consteval auto Layout() {
		return Fields<Motor::MotorHealth>()
			.Field<&Motor::MotorHealth::uptimeS>()
			.Field<&Motor::MotorHealth::resetReason>()
			.Field<&Motor::MotorHealth::state>();
	}
};


template<>
struct BodyOf<Probe::Flags> {
	static consteval auto Layout() {
		return Fields<Probe::Flags>()
			.Field<&Probe::Flags::armed>()
			.Field<&Probe::Flags::fault>();
	}
};


template<>
struct BodyOf<Probe::Wide> {
	static consteval auto Layout() {
		return Fields<Probe::Wide>()
			.Field<&Probe::Wide::offset>()
			.Field<&Probe::Wide::counter>()
			.Field<&Probe::Wide::trim>()
			.Field<&Probe::Wide::bias>();
	}
};


template<>
struct BodyOf<Probe::Shape> {
	static consteval auto Layout() {
		return Fields<Probe::Shape>()
			.Field<&Probe::Shape::status>()
			.Field<&Probe::Shape::gain>();
	}
};


template<>
struct BodyOf<Probe::Ladder> {
	static consteval auto Layout() {
		return Fields<Probe::Ladder>()
			.Field<&Probe::Ladder::steps>();
	}
};


template<>
struct BodyOf<Probe::Outer> {
	static consteval auto Layout() {
		return Fields<Probe::Outer>()
			.Field<&Probe::Outer::inner>()
			.Field<&Probe::Outer::tail>();
	}
};


template<>
struct BodyOf<Probe::Fleet> {
	static consteval auto Layout() {
		return Fields<Probe::Fleet>()
			.Field<&Probe::Fleet::reports>();
	}
};


template<>
struct BodyOf<Probe::Tail> {
	static consteval auto Layout() {
		return Fields<Probe::Tail>()
			.Field<&Probe::Tail::head>()
			.Field<&Probe::Tail::rest>()
			.Optional<&Probe::Tail::extra>()
			.Optional<&Probe::Tail::more>();
	}
};

}


// The sizes of the layouts: the required fields give the minimum, all the fields the maximum.
static_assert(VUB::BodyViewOf<Motor::MotorStart>.minSize == 3);
static_assert(VUB::BodyViewOf<Motor::MotorStart>.maxSize == 5);
static_assert(VUB::BodyViewOf<Motor::MotorReport>.minSize == 4);
static_assert(VUB::BodyViewOf<Motor::MotorReport>.maxSize == 4);
static_assert(VUB::BodyViewOf<Motor::MotorHealth>.maxSize == 6);
static_assert(VUB::BodyViewOf<Probe::Wide>.maxSize == 8 + 8 + 1 + 4);
static_assert(VUB::BodyViewOf<Probe::Shape>.maxSize == 5);
static_assert(VUB::BodyViewOf<Probe::Ladder>.maxSize == 6);
static_assert(VUB::BodyViewOf<Probe::Outer>.maxSize == 5);
static_assert(VUB::BodyViewOf<Probe::Fleet>.maxSize == 8);
static_assert(VUB::BodyViewOf<Probe::Tail>.minSize == 3);
static_assert(VUB::BodyViewOf<Probe::Tail>.maxSize == 5);
static_assert(VUB::BodyViewOf<VUB::NoBody>.maxSize == 0);
static_assert(VUB::HasBodyOf<Motor::MotorStart>);
static_assert(!VUB::HasBodyOf<int>);


// Encodes value and compares the bytes with the expected ones. Returns true when they are equal and of the same length.
template<typename T, std::size_t N>
bool EncodesTo(const T& value, const std::array<uint8, N>& expected) {
	std::array<uint8, 32> bytes{};
	Result<std::size_t> written = VUB::Encode(value, std::span<uint8>(bytes));

	if (!written.IsOk() || written.Value() != N) {
		return false;
	}

	for (std::size_t index = 0; index < N; index++) {
		if (bytes[index] != expected[index]) {
			return false;
		}
	}

	return true;
}


// Decodes the bytes into value. Returns the status.
template<typename T, std::size_t N>
ResultStatus DecodesFrom(const std::array<uint8, N>& bytes, T& value) {
	return VUB::Decode(std::span<const uint8>(bytes.data(), N), value);
}


void TestMotorStartEncodesToTheDesignBytes() {
	Motor::MotorStart start{};
	start.speedRpm = 1200;
	start.direction = 1;
	// Section 5.6: motor.start, MotorStart{1200, 1}, with ramp 300 by default: B0 04 01 2C 01.
	SIM_EXPECT(EncodesTo(start, std::array<uint8, 5>{0xB0, 0x04, 0x01, 0x2C, 0x01}));
}


void TestMotorReportEncodesToTheDesignBytes() {
	Motor::MotorReport report{};
	report.speedRpm = 1200;
	report.currentMa = 850;
	// Section 5.6: the final of motor.start with the report (1200 rpm, 850 mA): B0 04 52 03.
	SIM_EXPECT(EncodesTo(report, std::array<uint8, 4>{0xB0, 0x04, 0x52, 0x03}));
}


void TestMotorHealthEncodesToTheDesignBytes() {
	Motor::MotorHealth health{};
	health.uptimeS = 60;
	health.resetReason = 1;
	health.state = 2;
	// Section 5.6: the health body with uptime 60 s, reset 1, state 2: 3C 00 00 00 01 02.
	SIM_EXPECT(EncodesTo(health, std::array<uint8, 6>{0x3C, 0x00, 0x00, 0x00, 0x01, 0x02}));
}


void TestMotorStartDecodesTheVersionTwoForm() {
	Motor::MotorStart start{};
	const std::array<uint8, 5> bytes = {0xB0, 0x04, 0x01, 0x2C, 0x01};
	SIM_EXPECT(DecodesFrom(bytes, start) == ResultStatus::ok);
	SIM_EXPECT(start.speedRpm == 1200);
	SIM_EXPECT(start.direction == 1);
	SIM_EXPECT(start.rampMs == 300);
}


// A version one peer sends the three required bytes only: the optional ramp keeps its default.
void TestVersionOneFormKeepsTheRampDefault() {
	Motor::MotorStart start{};
	start.rampMs = 9;
	const std::array<uint8, 3> bytes = {0xB0, 0x04, 0x01};
	SIM_EXPECT(DecodesFrom(bytes, start) == ResultStatus::ok);
	SIM_EXPECT(start.speedRpm == 1200);
	SIM_EXPECT(start.direction == 1);
	SIM_EXPECT(start.rampMs == 300);
}


// A longer body from a newer peer: the bytes after the last known field are ignored.
void TestTrailingBytesAreIgnored() {
	Motor::MotorStart start{};
	const std::array<uint8, 7> bytes = {0xB0, 0x04, 0x01, 0x2C, 0x01, 0xEE, 0x77};
	SIM_EXPECT(DecodesFrom(bytes, start) == ResultStatus::ok);
	SIM_EXPECT(start.rampMs == 300);
}


// The required fields are missing: an empty body, and a body that ends in the middle of speedRpm, end with invalidData.
void TestMissingRequiredFieldIsInvalidData() {
	Motor::MotorStart start{};
	const std::array<uint8, 0> empty = {};
	const std::array<uint8, 2> half = {0xB0, 0x04};
	SIM_EXPECT(DecodesFrom(empty, start) == ResultStatus::invalidData);
	SIM_EXPECT(DecodesFrom(half, start) == ResultStatus::invalidData);
}


// A required field that is cut inside an optional one is also invalidData: the body is neither version.
void TestPartialOptionalFieldIsInvalidData() {
	Motor::MotorStart start{};
	const std::array<uint8, 4> cut = {0xB0, 0x04, 0x01, 0x2C};
	SIM_EXPECT(DecodesFrom(cut, start) == ResultStatus::invalidData);
}


void TestBoolAcceptsZeroAndOne() {
	Probe::Flags flags{};
	const std::array<uint8, 2> bytes = {0x01, 0x00};
	SIM_EXPECT(DecodesFrom(bytes, flags) == ResultStatus::ok);
	SIM_EXPECT(flags.armed);
	SIM_EXPECT(!flags.fault);
	SIM_EXPECT(EncodesTo(flags, std::array<uint8, 2>{0x01, 0x00}));
}


void TestBoolOtherThanZeroOrOneIsInvalidData() {
	Probe::Flags flags{};
	const std::array<uint8, 2> bytes = {0x01, 0x02};
	SIM_EXPECT(DecodesFrom(bytes, flags) == ResultStatus::invalidData);
}


// Signed and wide integers: little-endian, with the sign kept by the bit pattern.
void TestWideAndSignedIntegersAreLittleEndian() {
	Probe::Wide wide{};
	wide.offset = -2;
	wide.counter = 0x0102030405060708ull;
	wide.trim = -1;
	wide.bias = 0x11223344;
	const std::array<uint8, 21> expected = {
		0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
		0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
		0xFF,
		0x44, 0x33, 0x22, 0x11,
	};
	SIM_EXPECT(EncodesTo(wide, expected));

	std::array<uint8, 21> bytes{};
	SIM_EXPECT(VUB::Encode(wide, std::span<uint8>(bytes)).IsOk());
	SIM_EXPECT(bytes == expected);

	Probe::Wide back{};
	SIM_EXPECT(DecodesFrom(expected, back) == ResultStatus::ok);
	SIM_EXPECT(back.offset == -2);
	SIM_EXPECT(back.counter == 0x0102030405060708ull);
	SIM_EXPECT(back.trim == -1);
	SIM_EXPECT(back.bias == 0x11223344);
}


// An enum of up to 4 bytes is its underlying integer; a float is its IEEE binary32 bit pattern.
void TestEnumAndFloatFields() {
	Probe::Shape shape{};
	shape.status = Motor::MotorStatus::failed;
	shape.gain = 1.0f;
	SIM_EXPECT(EncodesTo(shape, std::array<uint8, 5>{0x02, 0x00, 0x00, 0x80, 0x3F}));

	Probe::Shape back{};
	// -2.0 is 0xC0000000: low byte first, 00 00 00 C0.
	const std::array<uint8, 5> bytes = {0x01, 0x00, 0x00, 0x00, 0xC0};
	SIM_EXPECT(DecodesFrom(bytes, back) == ResultStatus::ok);
	SIM_EXPECT(back.status == Motor::MotorStatus::warming);
	SIM_EXPECT(back.gain == -2.0f);
}


// A fixed array: its elements in order.
void TestArrayOfScalarsIsElementsInOrder() {
	Probe::Ladder ladder{};
	ladder.steps = {1, 0x0302, 0xFFFF};
	SIM_EXPECT(EncodesTo(ladder, std::array<uint8, 6>{0x01, 0x00, 0x02, 0x03, 0xFF, 0xFF}));

	Probe::Ladder back{};
	const std::array<uint8, 6> bytes = {0x01, 0x00, 0x02, 0x03, 0xFF, 0xFF};
	SIM_EXPECT(DecodesFrom(bytes, back) == ResultStatus::ok);
	SIM_EXPECT(back.steps[0] == 1);
	SIM_EXPECT(back.steps[1] == 0x0302);
	SIM_EXPECT(back.steps[2] == 0xFFFF);
}


// A nested body is inline: its fields come where it sits, and an array of nested bodies is their elements in order.
void TestNestedBodiesAreInline() {
	Probe::Outer outer{};
	outer.inner.speedRpm = 1200;
	outer.inner.currentMa = 850;
	outer.tail = 0x55;
	SIM_EXPECT(EncodesTo(outer, std::array<uint8, 5>{0xB0, 0x04, 0x52, 0x03, 0x55}));

	Probe::Fleet fleet{};
	fleet.reports[0].speedRpm = 1;
	fleet.reports[1].currentMa = 2;
	SIM_EXPECT(EncodesTo(fleet, std::array<uint8, 8>{0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00}));

	Probe::Fleet back{};
	const std::array<uint8, 8> bytes = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00};
	SIM_EXPECT(DecodesFrom(bytes, back) == ResultStatus::ok);
	SIM_EXPECT(back.reports[0].speedRpm == 1);
	SIM_EXPECT(back.reports[1].currentMa == 2);
}


// The optional fields of a body with two of them: with no bytes for either, both keep their defaults; with the first only, the second
// keeps its default.
void TestTrailingOptionalFieldsKeepTheirDefaults() {
	Probe::Tail tail{};
	const std::array<uint8, 3> headOnly = {0x0A, 0x0B, 0x00};
	SIM_EXPECT(DecodesFrom(headOnly, tail) == ResultStatus::ok);
	SIM_EXPECT(tail.head == 0x0A);
	SIM_EXPECT(tail.rest == 0x000B);
	SIM_EXPECT(tail.extra == 7);
	SIM_EXPECT(tail.more == 9);

	Probe::Tail one{};
	const std::array<uint8, 4> withExtra = {0x0A, 0x0B, 0x00, 0x21};
	SIM_EXPECT(DecodesFrom(withExtra, one) == ResultStatus::ok);
	SIM_EXPECT(one.extra == 0x21);
	SIM_EXPECT(one.more == 9);
}


// Encoding into a buffer shorter than the largest form gives messageTooLong and writes nothing.
void TestShortBufferIsMessageTooLong() {
	Motor::MotorStart start{};
	std::array<uint8, 4> bytes{};
	Result<std::size_t> written = VUB::Encode(start, std::span<uint8>(bytes));
	SIM_EXPECT(!written.IsOk());
	SIM_EXPECT(written.Error() == ResultStatus::messageTooLong);
}


// The body with no fields takes no bytes and ignores what it is given.
void TestNoBodyTakesNoBytes() {
	VUB::NoBody none{};
	std::array<uint8, 4> bytes{};
	Result<std::size_t> written = VUB::Encode(none, std::span<uint8>(bytes));
	SIM_EXPECT(written.IsOk());
	SIM_EXPECT(written.Value() == 0);

	const std::array<uint8, 2> anything = {0x01, 0x02};
	SIM_EXPECT(DecodesFrom(anything, none) == ResultStatus::ok);
}


// The character types are not fixed-width integers, so a body refuses them (the BuilderErrors snippet requests_body_wchar.cpp shows the
// message). Each fixed-width type is one, on every target.
void TestOnlyFixedWidthIntegersAreAccepted() {
	static_assert(VUB::IsFixedWidthInteger<int8>);
	static_assert(VUB::IsFixedWidthInteger<int16>);
	static_assert(VUB::IsFixedWidthInteger<int32>);
	static_assert(VUB::IsFixedWidthInteger<int64>);
	static_assert(VUB::IsFixedWidthInteger<uint8>);
	static_assert(VUB::IsFixedWidthInteger<uint16>);
	static_assert(VUB::IsFixedWidthInteger<uint32>);
	static_assert(VUB::IsFixedWidthInteger<uint64>);
	static_assert(!VUB::IsFixedWidthInteger<char>);
	static_assert(!VUB::IsFixedWidthInteger<wchar_t>);
	static_assert(!VUB::IsFixedWidthInteger<char8_t>);
	static_assert(!VUB::IsFixedWidthInteger<char16_t>);
	static_assert(!VUB::IsFixedWidthInteger<char32_t>);
}


int main() {
	TestMotorStartEncodesToTheDesignBytes();
	TestMotorReportEncodesToTheDesignBytes();
	TestMotorHealthEncodesToTheDesignBytes();
	TestMotorStartDecodesTheVersionTwoForm();
	TestVersionOneFormKeepsTheRampDefault();
	TestTrailingBytesAreIgnored();
	TestMissingRequiredFieldIsInvalidData();
	TestPartialOptionalFieldIsInvalidData();
	TestBoolAcceptsZeroAndOne();
	TestBoolOtherThanZeroOrOneIsInvalidData();
	TestWideAndSignedIntegersAreLittleEndian();
	TestEnumAndFloatFields();
	TestArrayOfScalarsIsElementsInOrder();
	TestNestedBodiesAreInline();
	TestTrailingOptionalFieldsKeepTheirDefaults();
	TestShortBufferIsMessageTooLong();
	TestNoBodyTakesNoBytes();
	TestOnlyFixedWidthIntegersAreAccepted();
	return SimFinish("TestVubRequestsBody");
}
