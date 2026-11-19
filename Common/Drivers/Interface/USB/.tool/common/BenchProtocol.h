#pragma once
#include <cstdint>
#include <bit>
#include <cstddef>
#include <cstring>
#include <span>


// Protocol between the USB benchmark firmware (.demo/STM32G4) and the PC benchmark (.tool/usbbench). Both sides include
// this header, so it uses only <cstdint> types and builds for the MCU and for the PC.
//
// The identity values are test values for these tools only. The library itself has no identity: an application gives
// its own vendor and product IDs, strings and GUIDs.
struct BenchProtocol {
	// pid.codes test VID/PID. Never ship a product with them.
	static constexpr std::uint16_t VendorId = 0x1209;
	static constexpr std::uint16_t ProductId = 0x0001;
	static constexpr std::uint16_t DeviceRelease = 0x0100;
	// Device interface GUID written into the Microsoft OS 2.0 registry property, so the WinUSB backend finds the device.
	static constexpr const char* InterfaceGuid = "{9CFFEE21-8C31-4DD2-ACB1-38A9CEB05DA4}";
	// Vendor code of the Microsoft OS 2.0 descriptor request (bmRequestType 0xC0, wIndex 7).
	static constexpr std::uint8_t MsOs20VendorCode = 0x20;
	static constexpr std::uint8_t InterfaceNumber = 0;
	static constexpr std::uint8_t BulkInAddress = 0x81;
	// 0x02, not 0x01: on fsdev the IN and OUT of one number share a register, so 0x81 and 0x01 would both stay single-buffered.
	static constexpr std::uint8_t BulkOutAddress = 0x02;
	static constexpr std::uint16_t BulkPacketSize = 64;


	// Vendor requests to the interface: bmRequestType 0x41 (OUT) or 0xC1 (IN), wIndex = InterfaceNumber.
	enum class Request : std::uint8_t {
		// OUT, no data. wValue = Mode. Resets both pattern generators and the counters.
		SetMode = 0x01,
		// IN, sizeof(Counters) bytes.
		GetCounters = 0x02,
		// OUT, no data. Zeroes the counters without changing the mode.
		ResetCounters = 0x03,
		// OUT, no data. wValue = milliseconds with the D+ pull-up off; the device then connects again and the host
		// enumerates it anew. The request is acknowledged before the disconnect.
		Reconnect = 0x04
	};


	enum class Mode : std::uint16_t {
		// Bulk IN sends nothing, bulk OUT data is accepted and dropped.
		Idle = 0,
		// Bulk IN streams the pattern without end.
		Source = 1,
		// Bulk OUT data is checked against the pattern.
		Sink = 2,
		// Source and Sink at the same time, each with its own generator.
		SourceAndSink = 3,
		// Bulk OUT data is sent back on bulk IN unchanged.
		Loopback = 4
	};


	// Little-endian, no padding: 8 + 8 + 4 + 4 + 4 + 4 = 32 bytes.
	struct Counters {
		std::uint64_t bytesSent;
		std::uint64_t bytesReceived;
		// Received bytes that differ from the pattern (Sink, SourceAndSink).
		std::uint32_t receiveErrors;
		// Offset of the first wrong byte in the received stream, 0xFFFFFFFF when there is none.
		std::uint32_t firstErrorOffset;
		std::uint32_t busResets;
		// Adapter errors (CRC, bit stuffing, overrun) the device counted.
		std::uint32_t usbErrors;
	};


	static constexpr std::size_t CountersSize = 32;
	// Pattern: xorshift32 words, little-endian bytes. Seed is the state after SetMode.
	static constexpr std::uint32_t PatternSeed = 0x12345678;


	// Next pattern word; state must not be 0.
	static constexpr std::uint32_t NextWord(std::uint32_t& state) {
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		return state;
	}


	// Byte generator over the word stream, so a transfer of any length continues the pattern exactly.
	struct PatternGenerator {
		std::uint32_t state = PatternSeed;
		std::uint32_t word = 0;
		std::uint8_t bytesLeft = 0;


		constexpr void Reset() {
			state = PatternSeed;
			word = 0;
			bytesLeft = 0;
		}


		constexpr std::uint8_t NextByte() {
			if (bytesLeft == 0) {
				word = NextWord(state);
				bytesLeft = 4;
			}

			std::uint8_t value = static_cast<std::uint8_t>(word);
			word >>= 8;
			bytesLeft--;
			return value;
		}


		// Stores the four bytes of value in little-endian order at destination. The littleEndian form is one memcpy of the
		// word's memory layout, so it is correct only on a little-endian host. The shift form is correct on any host. Fill()
		// passes the host's order. The host test passes both forms, since the PC is little-endian, so both run there.
		template<bool littleEndian>
		static void WriteWordBytes(std::uint8_t* destination, std::uint32_t value) {
			if constexpr (littleEndian) {
				std::memcpy(destination, &value, sizeof(value));
			} else {
				destination[0] = static_cast<std::uint8_t>(value);
				destination[1] = static_cast<std::uint8_t>(value >> 8);
				destination[2] = static_cast<std::uint8_t>(value >> 16);
				destination[3] = static_cast<std::uint8_t>(value >> 24);
			}
		}


		// Writes the next data.size() bytes: the same bytes, and the same final state, as NextByte() called data.size() times.
		// The fill runs in the USB interrupt when no IN stage is ready, and in the main loop otherwise, so a byte loop costs
		// too much per packet. Once the generator is word-aligned, a whole word is one NextWord() and four stores. The edges
		// use NextByte(), because a partly used word and a tail shorter than a word have no whole-word form. A word-aligned
		// generator always has word == 0 (NextByte() shifts the word out), so the whole-word loop leaves word alone.
		void Fill(std::span<std::uint8_t> data) {
			std::size_t index = 0;
			while (index < data.size() && bytesLeft != 0) {
				data[index] = NextByte();
				index++;
			}

			while (data.size() - index >= 4) {
				std::uint32_t value = NextWord(state);
				WriteWordBytes<std::endian::native == std::endian::little>(data.data() + index, value);
				index += 4;
			}

			while (index < data.size()) {
				data[index] = NextByte();
				index++;
			}
		}
	};
};


static_assert(sizeof(BenchProtocol::Counters) == BenchProtocol::CountersSize);
