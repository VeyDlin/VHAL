// Every error row of Decode in the COBS wiki, with the status the row names, and the leading-zero rule that Decode applies before the
// first block of a frame.
#include <Utilities/Encoding/COBS/Cobs.h>
#include <cstddef>
#include <cstdio>
#include <vector>

using Encoding::Cobs;

static int failures = 0;

static void Check(bool condition, const char* what) {
	if (!condition) {
		std::printf("FAILED: %s\n", what);
		failures++;
	}
}


static void CheckDecodeError(const std::vector<uint8>& frame, std::size_t outSize, ResultStatus expected, const char* what) {
	std::vector<uint8> out(outSize);
	Result<std::size_t> result = Cobs::Decode(std::span<const uint8>(frame), std::span<uint8>(out));
	Check(result.IsErr() && result.Error() == expected, what);
}


static void CheckDecodeValue(const std::vector<uint8>& frame, const std::vector<uint8>& expected, const char* what) {
	std::vector<uint8> out(expected.size() + 1, 0xEE);
	Result<std::size_t> result = Cobs::Decode(std::span<const uint8>(frame), std::span<uint8>(out));
	Check(result.IsOk() && result.Value() == expected.size(), what);
	if (result.IsOk()) {
		out.resize(result.Value());
		Check(out == expected, what);
	}
}


static void TestInvalidArgument() {
	CheckDecodeError({}, 8, ResultStatus::invalidArgument, "empty input is invalidArgument");
	CheckDecodeError({}, 0, ResultStatus::invalidArgument, "empty input is invalidArgument with an empty out");
}


static void TestNoDelimiter() {
	CheckDecodeError({ 0x03, 0x11, 0x22 }, 8, ResultStatus::dataCorrupted, "a block with no delimiter is dataCorrupted");
	CheckDecodeError({ 0x01 }, 8, ResultStatus::dataCorrupted, "a code byte with no delimiter is dataCorrupted");
	CheckDecodeError({ 0x02, 0x11 }, 8, ResultStatus::dataCorrupted, "data with no delimiter is dataCorrupted");
	CheckDecodeError({ 0x01, 0x01 }, 8, ResultStatus::dataCorrupted, "a frame with a zero block and no delimiter is dataCorrupted");
}


static void TestBlockRunsIntoDelimiter() {
	CheckDecodeError({ 0x05, 0x11, 0x00 }, 8, ResultStatus::dataCorrupted, "a block that runs into a zero is dataCorrupted");
	CheckDecodeError({ 0x03, 0x11, 0x00, 0x22, 0x00 }, 8, ResultStatus::dataCorrupted, "a block cut by a zero, with bytes after it, is dataCorrupted");
	CheckDecodeError({ 0x04, 0x00, 0x00, 0x00, 0x00 }, 8, ResultStatus::dataCorrupted, "a block of four cut by a zero is dataCorrupted");
}


static void TestZeroBeforeLastByte() {
	// The frame 02 11 00 ends at its first zero, which is not the last byte of the input.
	CheckDecodeError({ 0x02, 0x11, 0x00, 0x02, 0x22, 0x00 }, 8, ResultStatus::dataCorrupted, "a delimiter before the end of in is dataCorrupted");
	CheckDecodeError({ 0x01, 0x00, 0x00 }, 8, ResultStatus::dataCorrupted, "an empty frame followed by a zero is dataCorrupted");
	CheckDecodeError({ 0x01, 0x00, 0x01, 0x00 }, 8, ResultStatus::dataCorrupted, "two frames in one input are dataCorrupted");
	CheckDecodeError({ 0x03, 0x11, 0x22, 0x00, 0x00 }, 8, ResultStatus::dataCorrupted, "a zero after the delimiter is dataCorrupted");
}


static void TestLeadingZeros() {
	// A zero byte before the first block is nothing: it does not end a frame, so it is not a delimiter before the last byte.
	CheckDecodeValue({ 0x00, 0x01, 0x00 }, {}, "a leading zero before the empty frame is ignored");
	CheckDecodeValue({ 0x00, 0x00, 0x03, 0x11, 0x22, 0x00 }, { 0x11, 0x22 }, "two leading zeros before a frame are ignored");
	CheckDecodeValue({ 0x00, 0x01, 0x01, 0x00 }, { 0x00 }, "a leading zero before a frame of one zero is ignored");
	CheckDecodeError({ 0x00, 0x00, 0x00 }, 8, ResultStatus::dataCorrupted, "zeros alone have no delimiter and are dataCorrupted");
	CheckDecodeError({ 0x00 }, 8, ResultStatus::dataCorrupted, "one zero alone has no delimiter and is dataCorrupted");
}


static void TestBufferOverflow() {
	CheckDecodeError({ 0x03, 0x11, 0x22, 0x00 }, 1, ResultStatus::bufferOverflow, "a packet longer than out is bufferOverflow");
	CheckDecodeError({ 0x03, 0x11, 0x22, 0x00 }, 0, ResultStatus::bufferOverflow, "a packet into an empty out is bufferOverflow");
	const std::vector<uint8> emptyFrame = { 0x01, 0x00 };
	std::vector<uint8> empty;
	Result<std::size_t> emptyResult = Cobs::Decode(std::span<const uint8>(emptyFrame), std::span<uint8>(empty));
	Check(emptyResult.IsOk() && emptyResult.Value() == 0, "an empty packet decodes into an empty out");
	const std::vector<uint8> frame = { 0x03, 0x11, 0x22, 0x00 };
	std::vector<uint8> exact(2);
	Result<std::size_t> result = Cobs::Decode(std::span<const uint8>(frame), std::span<uint8>(exact));
	Check(result.IsOk() && result.Value() == 2, "a packet of exactly the size of out decodes");
}


int main() {
	TestInvalidArgument();
	TestNoDelimiter();
	TestBlockRunsIntoDelimiter();
	TestZeroBeforeLastByte();
	TestLeadingZeros();
	TestBufferOverflow();

	if (failures != 0) {
		std::printf("%d check(s) failed\n", failures);
		return 1;
	}

	std::printf("TestCobsDecodeErrors: all checks passed\n");
	return 0;
}
