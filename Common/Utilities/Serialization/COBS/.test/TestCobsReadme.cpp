#include <VHAL.h>
#include <Utilities/Serialization/COBS/COBS.h>
#include <cstdio>


// The examples of the COBS wiki page (Serialization/COBS), run as the page writes them. The values the examples give are checked.

namespace {


int failures = 0;


void Check(bool condition, const char* what) {
	if (!condition) {
		failures++;
		std::printf("TestCobsReadme.cpp: %s\n", what);
	}
}


void QuickStart() {
	// Create encoder/decoder with default config
	COBS<128> cobs({ });

	uint8 input[] = { 0x10, 0x20, 0x00, 0x30 };
	uint8 encoded[128];
	uint8 decoded[128];

	// Encode
	Result<size_t> encResult = cobs.Encode(input, sizeof(input), encoded, sizeof(encoded));
	if (encResult.IsOk()) {
		size_t encodedLength = encResult.Value();

		// Decode
		Result<size_t> decResult = cobs.Decode(encoded, encodedLength, decoded, sizeof(decoded));
		if (decResult.IsOk()) {
			size_t decodedLength = decResult.Value();
			// decoded now matches input
			Check(decodedLength == sizeof(input), "the quick start decodes the input length");
			Check(decoded[0] == 0x10 && decoded[1] == 0x20 && decoded[2] == 0x00 && decoded[3] == 0x30, "the quick start decodes the input");
		}
	}

	Check(encResult.IsOk() && encResult.Value() == 8, "the quick start frame is eight bytes");
}


void CustomConfiguration() {
	COBS<256> cobs({
		.startByte    = 0xAA,
		.stopByte     = 0x55,
		.escapeByte   = 0xBB,
		.escapedStart = 0x01,
		.escapedStop  = 0x02,
		.escapedEscape = 0x03
	});

	Check(cobs.GetConfig().startByte == 0xAA && cobs.GetConfig().stopByte == 0x55, "the custom configuration keeps its marker bytes");
}


void GetConfigExample() {
	COBS<128> cobs({ });

	// The configuration the instance was created with
	COBS<128>::Config config = cobs.GetConfig();
	Check(config.startByte == 0xFF && config.stopByte == 0x00 && config.escapeByte == 0xFE, "GetConfig returns the default configuration");
}


void EncodeExample() {
	COBS<128> cobs({ });

	uint8 payload[] = { 0x01, 0x02, 0x03 };
	uint8 frame[128];

	Result<size_t> result = cobs.Encode(payload, 3, frame, sizeof(frame));
	if (result.IsOk()) {
		// frame[0]              == 0xFF (startByte)
		// frame[result.Value() - 1] == 0x00 (stopByte)
		// Send frame, result.Value() bytes total
		Check(frame[0] == 0xFF, "the frame starts with the start byte");
		Check(frame[result.Value() - 1] == 0x00, "the frame ends with the stop byte");
	}
}


void DecodeExample() {
	COBS<128> cobs({ });

	// The frame of 10 20 00 30 with the default configuration
	uint8 frame[] = { 0xFF, 0x06, 0x10, 0x20, 0xFE, 0x02, 0x30, 0x00 };
	size_t frameLen = sizeof(frame);

	// Assume 'frame' and 'frameLen' come from Encode or a serial port
	uint8 decoded[128];
	Result<size_t> result = cobs.Decode(frame, frameLen, decoded, sizeof(decoded));
	if (result.IsOk()) {
		// Use result.Value() decoded bytes
		Check(result.Value() == 4 && decoded[2] == 0x00, "the frame decodes to 10 20 00 30");
	}
}
}


int main() {
	QuickStart();
	CustomConfiguration();
	GetConfigExample();
	EncodeExample();
	DecodeExample();

	if (failures != 0) {
		std::printf("%d COBS readme check(s) failed\n", failures);
		return 1;
	}

	std::printf("COBS readme examples passed\n");
	return 0;
}
