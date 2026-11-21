#pragma once
#include <Utilities/DataTypes.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <span>


// The NTB-16 codec of the CDC-NCM function (USB CDC NCM 1.0, section 3). A network transfer block (NTB) carries whole Ethernet
// frames over the bulk endpoints. Its layout, as this codec reads and writes it:
//   NTH16 header   offset 0, 12 bytes: "NCMH", wHeaderLength 12, wSequence, wBlockLength, wNdpIndex;
//   datagrams      Ethernet frames without FCS, each at an offset that the advertised divisor and remainder allow;
//   NDP16          at wNdpIndex: "NCM0", wLength, wNextNdpIndex, then (wDatagramIndex, wDatagramLength) pairs ending with (0, 0).
// Every field is little endian. This file has no USB device dependency: the function uses it to parse the host's NTBs and to build
// the ones it sends, and the tests use it on its own.
//
// Parser decisions. A block with any rule broken is dropped whole: no datagram of it is delivered, so the function never hands the
// network stack a part of a block that the host did not mean. The rules are checked in one walk, and the walk is what delivers the
// datagrams, so the check and the delivery cannot disagree. A block length of 0 is invalid in NTB-16 (the zero length is an NTB-32
// convention). Bytes received after wBlockLength are ignored, so a host that pads the transfer is accepted. The NDP chain is bounded:
// an NDP is at least 16 bytes and a pair is 4, so a walk that reads more NDPs or pairs than the block could hold is refused as a
// cyclic or overlapping chain. Datagram offsets are not checked against the divisor, because the host's alignment is its own choice.
// Entries after the first (0, 0) pair of an NDP are not read. CRC datagrams ("NCM1") are refused, as CRC mode is not advertised.


// The sizes of the NTB-16 structures. The header and the NDP header are the fixed parts; each pair is 4 bytes.
constexpr uint32 USBNcmNth16Size = 12;
constexpr uint32 USBNcmNdp16HeaderSize = 8;
constexpr uint32 USBNcmNdp16MinSize = 16;
constexpr uint32 USBNcmNdp16EntrySize = 4;
constexpr uint32 USBNcmNtbParametersSize = 28;
// wHeaderLength of the NTH16: the header size, which is the first datagram's lowest offset.
constexpr uint16 USBNcmNthHeaderLength = 12;
// wLength of the NTB parameter structure: the structure's own size.
constexpr uint16 USBNcmNtbParametersLength = 28;
// The largest block NTB-16 can address: wBlockLength and wNdpIndex are 16 bits.
constexpr uint32 USBNcmNtb16MaxSize = 0xFFFF;
// The smallest NTB size a device may advertise, for both dwNtbInMaxSize and dwNtbOutMaxSize.
constexpr uint32 USBNcmMinNtbSize = 2048;
// The smallest Ethernet frame (without FCS) carried as a datagram, and the default largest one (a 1500-byte payload plus the header).
constexpr uint32 USBNcmMinDatagramSize = 14;
constexpr uint32 USBNcmDefaultMaxDatagramSize = 1514;
// The default bmNtbFormatsSupported bit for NTB-16, and the NDP divisor and alignment of the default parameters.
constexpr uint16 USBNcmNtbFormatNtb16 = 0x0001;
constexpr uint16 USBNcmNtbFormatNtb32 = 0x0002;
constexpr uint16 USBNcmDefaultNdpDivisor = 4;
constexpr uint16 USBNcmDefaultNdpAlignment = 4;

// The signatures as they appear on the wire. The NTH16 reads "NCMH". The NDP16 reads "NCM0" (no CRC) or "NCM1" (each datagram carries
// a CRC); this codec refuses "NCM1".
constexpr std::array<uint8, 4> USBNcmNth16Signature = {0x4E, 0x43, 0x4D, 0x48};
constexpr std::array<uint8, 4> USBNcmNdp16Signature = {0x4E, 0x43, 0x4D, 0x30};
constexpr std::array<uint8, 4> USBNcmNdp16CrcSignature = {0x4E, 0x43, 0x4D, 0x31};


// The NTB parameter structure of GET_NTB_PARAMETERS (NCM 1.0 table 6-3), 28 bytes. The function fills it in and the host reads it. The
// defaults are the smallest sizes the specification allows, with NTB-16 only and no datagram limit.
struct USBNcmNtbParameters {
	// bmNtbFormatsSupported: bit 0 is NTB-16, bit 1 NTB-32.
	uint16 formatsSupported = USBNcmNtbFormatNtb16;
	// dwNtbInMaxSize is the largest NTB the device sends. The host lowers its own input size below it with SET_NTB_INPUT_SIZE, and that
	// size is not part of these parameters. dwNtbOutMaxSize is the largest NTB the host sends to the device. The parser checks received
	// blocks against outMaxSize, the builder against inMaxSize.
	uint32 inMaxSize = USBNcmMinNtbSize;
	uint16 ndpInDivisor = USBNcmDefaultNdpDivisor;
	uint16 ndpInPayloadRemainder = 0;
	uint16 ndpInAlignment = USBNcmDefaultNdpAlignment;
	uint32 outMaxSize = USBNcmMinNtbSize;
	uint16 ndpOutDivisor = USBNcmDefaultNdpDivisor;
	uint16 ndpOutPayloadRemainder = 0;
	uint16 ndpOutAlignment = USBNcmDefaultNdpAlignment;
	// wNtbOutMaxDatagrams: the most datagrams the device accepts in one block it receives. 0 is no limit.
	uint16 outMaxDatagrams = 0;
};


// The limits one direction of the codec works within. For a parser these are the advertised OUT limits: dwNtbOutMaxSize, the OUT
// datagram count and the largest frame. For a builder they are the host's current input size (SET_NTB_INPUT_SIZE), the count the host
// set with it, and the largest frame the function sends. A count of 0 is no limit.
struct USBNcmNtbLimits {
	uint32 maxNtbSize = USBNcmMinNtbSize;
	uint32 maxDatagrams = 0;
	uint32 maxDatagramSize = USBNcmDefaultMaxDatagramSize;
};


// The outcome of a check of a received block. ok means every rule holds; every other value names the first rule that failed, for the
// function's error counters.
enum class USBNcmNtb16Status : uint8 {
	ok,
	// The received bytes are shorter than the NTH16 header.
	headerTooShort,
	// The bytes at offset 0 are not "NCMH".
	badSignature,
	// wHeaderLength is not 12.
	badHeaderLength,
	// wBlockLength is below the header size, which includes 0.
	blockTooSmall,
	// wBlockLength is more than the received bytes or more than dwNtbOutMaxSize.
	blockTooLarge,
	// wNdpIndex, or wNextNdpIndex of a later NDP, is below 12, not a multiple of 4, or leaves no room for the NDP header in the block.
	ndpIndexInvalid,
	// The NDP does not start with "NCM0" or "NCM1".
	ndpSignature,
	// The NDP starts with "NCM1": each datagram would carry a CRC, which this codec does not accept.
	ndpCrcUnsupported,
	// wLength of the NDP is below 16, not a multiple of 4, or runs past the block.
	ndpLengthInvalid,
	// No (0, 0) pair lies inside the NDP's wLength.
	ndpUnterminated,
	// A datagram starts below the header or ends past the block.
	datagramOutsideBlock,
	// A datagram is shorter than an Ethernet header or longer than the largest frame.
	datagramSizeInvalid,
	// The block lists more datagrams than wNtbOutMaxDatagrams allows.
	tooManyDatagrams,
	// The chain has more NDPs or pairs than the block could hold: a cycle or an overlap.
	ndpChainTooLong,
};


// Little-endian field helpers. Put writes into bytes that the caller has sized. Read16 expects offset + 2 to be inside bytes; the parser
// checks that before it reads, so a hostile length cannot reach it.
constexpr void USBNcmPut16(std::span<uint8> bytes, uint32 offset, uint32 value) {
	bytes[offset] = static_cast<uint8>(value & 0xFF);
	bytes[offset + 1] = static_cast<uint8>((value >> 8) & 0xFF);
}


constexpr void USBNcmPut32(std::span<uint8> bytes, uint32 offset, uint32 value) {
	for (uint32 index = 0; index < 4; index++) {
		bytes[offset + index] = static_cast<uint8>((value >> (8 * index)) & 0xFF);
	}
}


constexpr uint16 USBNcmRead16(std::span<const uint8> bytes, uint32 offset) {
	return static_cast<uint16>(bytes[offset] | (bytes[offset + 1] << 8));
}


// True when the signature lies inside bytes at offset and matches it.
template<std::size_t Size>
constexpr bool USBNcmMatches(std::span<const uint8> bytes, uint32 offset, const std::array<uint8, Size>& signature) {
	if (static_cast<std::size_t>(offset) + Size > bytes.size()) {
		return false;
	}

	for (std::size_t index = 0; index < Size; index++) {
		if (bytes[offset + index] != signature[index]) {
			return false;
		}
	}

	return true;
}


// Encodes the 28-byte NTB parameter structure. wLength is 28 and wReserved is 0.
constexpr std::array<uint8, USBNcmNtbParametersSize> USBNcmEncodeNtbParameters(const USBNcmNtbParameters& parameters) {
	std::array<uint8, USBNcmNtbParametersSize> bytes{};
	USBNcmPut16(bytes, 0, USBNcmNtbParametersLength);
	USBNcmPut16(bytes, 2, parameters.formatsSupported);
	USBNcmPut32(bytes, 4, parameters.inMaxSize);
	USBNcmPut16(bytes, 8, parameters.ndpInDivisor);
	USBNcmPut16(bytes, 10, parameters.ndpInPayloadRemainder);
	USBNcmPut16(bytes, 12, parameters.ndpInAlignment);
	USBNcmPut16(bytes, 14, 0);
	USBNcmPut32(bytes, 16, parameters.outMaxSize);
	USBNcmPut16(bytes, 20, parameters.ndpOutDivisor);
	USBNcmPut16(bytes, 22, parameters.ndpOutPayloadRemainder);
	USBNcmPut16(bytes, 24, parameters.ndpOutAlignment);
	USBNcmPut16(bytes, 26, parameters.outMaxDatagrams);
	return bytes;
}


// True when value is a power of two and at least 4, the form the NDP divisors and alignments take.
constexpr bool USBNcmIsPowerOfTwoAtLeastFour(uint32 value) {
	return value >= 4 && (value & (value - 1)) == 0;
}


// True when every size and alignment the parameters carry is one the codec can use: the NTB sizes are at least 2048, the divisors and
// alignments are powers of two of at least 4, and each payload remainder is below its divisor.
constexpr bool USBNcmNtbParametersValid(const USBNcmNtbParameters& parameters) {
	if (parameters.inMaxSize < USBNcmMinNtbSize || parameters.outMaxSize < USBNcmMinNtbSize) {
		return false;
	}

	if (!USBNcmIsPowerOfTwoAtLeastFour(parameters.ndpInDivisor) || !USBNcmIsPowerOfTwoAtLeastFour(parameters.ndpInAlignment)) {
		return false;
	}

	if (!USBNcmIsPowerOfTwoAtLeastFour(parameters.ndpOutDivisor) || !USBNcmIsPowerOfTwoAtLeastFour(parameters.ndpOutAlignment)) {
		return false;
	}

	return parameters.ndpInPayloadRemainder < parameters.ndpInDivisor && parameters.ndpOutPayloadRemainder < parameters.ndpOutDivisor;
}


namespace USBNcmNtb16Detail {


// Walks the NDP chain of a received block and calls visit for each datagram, in the order the NDPs list them. Every rule is checked
// here, so the same walk serves the check (with a visitor that does nothing) and the delivery. A datagram is visited only after its
// own checks pass, and the walk stops at the first rule that fails.
template<typename TVisit>
USBNcmNtb16Status Walk(std::span<const uint8> input, const USBNcmNtbLimits& limits, TVisit& visit) {
	if (input.size() < USBNcmNth16Size) {
		return USBNcmNtb16Status::headerTooShort;
	}

	if (!USBNcmMatches(input, 0, USBNcmNth16Signature)) {
		return USBNcmNtb16Status::badSignature;
	}

	uint32 headerLength = USBNcmRead16(input, 4);
	if (headerLength != USBNcmNthHeaderLength) {
		return USBNcmNtb16Status::badHeaderLength;
	}

	uint32 blockLength = USBNcmRead16(input, 8);
	if (blockLength < USBNcmNth16Size) {
		return USBNcmNtb16Status::blockTooSmall;
	}

	if (blockLength > input.size() || blockLength > limits.maxNtbSize) {
		return USBNcmNtb16Status::blockTooLarge;
	}

	uint32 ndpOffset = USBNcmRead16(input, 10);
	// The bounds on the walk: an NDP is at least 16 bytes and a pair 4, and the NDPs of a block do not overlap, so a block holds at most
	// blockLength / 16 NDPs and blockLength / 4 pairs. A walk past either is a cycle or an overlap, and it stops here.
	// A block of 12 to 15 bytes holds no NDP, so its first NDP is the one that is refused, by the index check below. The count keeps at
	// least one NDP, so that the refusal is the index rule, not a chain that is too long.
	uint32 maxNdps = std::max<uint32>(1, blockLength / USBNcmNdp16MinSize);
	uint32 maxPairs = blockLength / USBNcmNdp16EntrySize;
	uint32 ndpsRead = 0;
	uint32 pairsRead = 0;
	uint32 datagrams = 0;
	while (true) {
		ndpsRead++;
		if (ndpsRead > maxNdps) {
			return USBNcmNtb16Status::ndpChainTooLong;
		}

		if (ndpOffset < USBNcmNth16Size || ndpOffset % 4 != 0 || ndpOffset + USBNcmNdp16HeaderSize > blockLength) {
			return USBNcmNtb16Status::ndpIndexInvalid;
		}

		if (!USBNcmMatches(input, ndpOffset, USBNcmNdp16Signature)) {
			if (USBNcmMatches(input, ndpOffset, USBNcmNdp16CrcSignature)) {
				return USBNcmNtb16Status::ndpCrcUnsupported;
			}

			return USBNcmNtb16Status::ndpSignature;
		}

		uint32 ndpLength = USBNcmRead16(input, ndpOffset + 4);
		if (ndpLength < USBNcmNdp16MinSize || ndpLength % 4 != 0 || ndpOffset + ndpLength > blockLength) {
			return USBNcmNtb16Status::ndpLengthInvalid;
		}

		uint32 nextOffset = USBNcmRead16(input, ndpOffset + 6);
		uint32 ndpEnd = ndpOffset + ndpLength;
		bool terminated = false;
		for (uint32 entry = ndpOffset + USBNcmNdp16HeaderSize; entry + USBNcmNdp16EntrySize <= ndpEnd; entry += USBNcmNdp16EntrySize) {
			pairsRead++;
			if (pairsRead > maxPairs) {
				return USBNcmNtb16Status::ndpChainTooLong;
			}

			uint32 datagramOffset = USBNcmRead16(input, entry);
			uint32 datagramSize = USBNcmRead16(input, entry + 2);
			if (datagramOffset == 0 && datagramSize == 0) {
				terminated = true;
				break;
			}

			if (datagramOffset < USBNcmNth16Size || datagramOffset + datagramSize > blockLength) {
				return USBNcmNtb16Status::datagramOutsideBlock;
			}

			if (datagramSize < USBNcmMinDatagramSize || datagramSize > limits.maxDatagramSize) {
				return USBNcmNtb16Status::datagramSizeInvalid;
			}

			datagrams++;
			if (limits.maxDatagrams != 0 && datagrams > limits.maxDatagrams) {
				return USBNcmNtb16Status::tooManyDatagrams;
			}

			visit(input.subspan(datagramOffset, datagramSize));
		}

		if (!terminated) {
			return USBNcmNtb16Status::ndpUnterminated;
		}

		if (nextOffset == 0) {
			return USBNcmNtb16Status::ok;
		}

		ndpOffset = nextOffset;
	}
}


}


// Checks a block the host sent against the limits. Returns ok when every rule holds; then the block's datagrams may be used through
// USBNcmNtb16ForEachDatagram. Nothing is delivered here.
inline USBNcmNtb16Status USBNcmNtb16Check(std::span<const uint8> input, const USBNcmNtbLimits& limits) {
	auto ignore = [](std::span<const uint8>) {
	};
	return USBNcmNtb16Detail::Walk(input, limits, ignore);
}


// Calls visit with each datagram of a block the host sent, in the order its NDPs list them. The datagrams are spans into input, and they
// are valid only while input is. A block that fails the check delivers nothing and returns the rule that failed; visit is never called
// for a block with a broken rule, so a bad block cannot leave the function with a part of its datagrams.
template<typename TVisit>
USBNcmNtb16Status USBNcmNtb16ForEachDatagram(std::span<const uint8> input, const USBNcmNtbLimits& limits, TVisit visit) {
	USBNcmNtb16Status status = USBNcmNtb16Check(input, limits);
	if (status != USBNcmNtb16Status::ok) {
		return status;
	}

	return USBNcmNtb16Detail::Walk(input, limits, visit);
}


// Assembles one NTB to send to the host. The block is written into the caller's buffer, which holds the NTB until the function sends
// it. The layout the builder writes:
//   NTH16 at offset 0, then the datagrams, each at an offset the IN divisor and remainder allow (the zero bytes before one are padding);
//   one NDP16 after the last datagram, at an offset the IN alignment allows. The NDP follows the datagrams, not precedes them, because
//   its size depends on how many datagrams the block holds. Its room (the NDP and its terminator) is reserved by TryAdd as each
//   datagram is added, so the block never outgrows its limit, and a datagram that does not fit is refused with nothing written.
//
// The block size is the smallest of the host's NTB input size, dwNtbInMaxSize and the buffer size, and never more than 65535, the
// NTB-16 limit. The datagram count is the smaller of the host's count and MaxDatagramsPerNtb, the fixed room the builder keeps for the
// NDP entries; the entries are held here until Finish writes them, so the builder needs no allocation.
//
// The sequence number starts at 0 and goes up by one per block that Finish writes, wrapping at 65536. A block with no datagram is not
// written: Finish returns 0 and nothing is sent.
template<uint16 MaxDatagramsPerNtb = 128>
class USBNcmNtb16Builder {
	static_assert(MaxDatagramsPerNtb > 0, "A block holds at least one datagram");


private:
	std::span<uint8> buffer;
	USBNcmNtbParameters parameters;
	std::array<uint16, MaxDatagramsPerNtb> entryIndex{};
	std::array<uint16, MaxDatagramsPerNtb> entryLength{};
	uint32 blockLimit = 0;
	uint32 dataEnd = 0;
	uint32 datagramCount = 0;
	uint32 maxDatagrams = 0;
	uint32 maxDatagramSize = 0;
	uint16 sequence = 0;
	bool started = false;


public:
	USBNcmNtb16Builder(std::span<uint8> bufferToFill, const USBNcmNtbParameters& parametersToUse)
		: buffer(bufferToFill), parameters(parametersToUse) {
	}


	// Starts the next block under the host's current limits. Returns false, and starts nothing, when the parameters are not valid or the
	// buffer cannot hold a block with no datagram.
	bool Reset(const USBNcmNtbLimits& limits) {
		started = false;
		if (!USBNcmNtbParametersValid(parameters)) {
			return false;
		}

		uint32 size = std::min(limits.maxNtbSize, parameters.inMaxSize);
		size = std::min(size, static_cast<uint32>(buffer.size()));
		size = std::min(size, USBNcmNtb16MaxSize);
		if (size < USBNcmNth16Size + USBNcmNdp16MinSize) {
			return false;
		}

		blockLimit = size;
		maxDatagrams = limits.maxDatagrams;
		maxDatagramSize = limits.maxDatagramSize;
		dataEnd = USBNcmNth16Size;
		datagramCount = 0;
		started = true;
		return true;
	}


	// Appends one Ethernet frame. Returns false, and writes nothing, when the frame is not a valid datagram for this block or when the
	// frame and the NDP that lists it would not fit the block.
	bool TryAdd(std::span<const uint8> frame) {
		if (!started) {
			return false;
		}

		uint32 frameSize = static_cast<uint32>(frame.size());
		if (frameSize < USBNcmMinDatagramSize || frameSize > maxDatagramSize) {
			return false;
		}

		if (datagramCount >= static_cast<uint32>(MaxDatagramsPerNtb)) {
			return false;
		}

		if (maxDatagrams != 0 && datagramCount >= maxDatagrams) {
			return false;
		}

		uint32 frameOffset = AlignPayload(dataEnd);
		uint32 frameEnd = frameOffset + frameSize;
		uint32 ndpOffset = AlignNdp(frameEnd);
		// The NDP that would list this frame holds datagramCount + 1 pairs and the (0, 0) terminator.
		uint32 blockEnd = ndpOffset + NdpSize(datagramCount + 2);
		if (blockEnd > blockLimit) {
			return false;
		}

		for (uint32 index = dataEnd; index < frameOffset; index++) {
			buffer[index] = 0;
		}

		std::copy(frame.begin(), frame.end(), buffer.begin() + frameOffset);
		entryIndex[datagramCount] = static_cast<uint16>(frameOffset);
		entryLength[datagramCount] = static_cast<uint16>(frameSize);
		datagramCount++;
		dataEnd = frameEnd;
		return true;
	}


	// Writes the NTH16 and the NDP16 and returns the block's length, or 0 when no datagram was added. The block is then complete and the
	// builder must be Reset before the next one.
	uint32 Finish() {
		if (!started || datagramCount == 0) {
			started = false;
			return 0;
		}

		started = false;
		uint32 ndpOffset = AlignNdp(dataEnd);
		uint32 ndpSize = NdpSize(datagramCount + 1);
		uint32 blockSize = ndpOffset + ndpSize;
		for (uint32 index = dataEnd; index < ndpOffset; index++) {
			buffer[index] = 0;
		}

		std::copy(USBNcmNth16Signature.begin(), USBNcmNth16Signature.end(), buffer.begin());
		USBNcmPut16(buffer, 4, USBNcmNthHeaderLength);
		USBNcmPut16(buffer, 6, sequence);
		USBNcmPut16(buffer, 8, blockSize);
		USBNcmPut16(buffer, 10, ndpOffset);

		std::copy(USBNcmNdp16Signature.begin(), USBNcmNdp16Signature.end(), buffer.begin() + ndpOffset);
		USBNcmPut16(buffer, ndpOffset + 4, ndpSize);
		USBNcmPut16(buffer, ndpOffset + 6, 0);
		for (uint32 index = 0; index < datagramCount; index++) {
			uint32 entryOffset = ndpOffset + USBNcmNdp16HeaderSize + index * USBNcmNdp16EntrySize;
			USBNcmPut16(buffer, entryOffset, entryIndex[index]);
			USBNcmPut16(buffer, entryOffset + 2, entryLength[index]);
		}

		// The terminator and the bytes after it are zero, so the NDP never has stale bytes past its last pair.
		for (uint32 index = ndpOffset + USBNcmNdp16HeaderSize + datagramCount * USBNcmNdp16EntrySize; index < blockSize; index++) {
			buffer[index] = 0;
		}

		sequence = static_cast<uint16>(sequence + 1);
		return blockSize;
	}


	// Sets the sequence number of the next block. A session starts at 0 (the function calls this when the data interface selects its
	// operating alternate), so the host sees the sequence restart with the session.
	void SetSequence(uint16 value) {
		sequence = value;
	}


	// The sequence number the next block will carry. A caller that discards a finished block sets it back, so the number is not skipped.
	uint16 Sequence() const {
		return sequence;
	}


private:
	// The offset of the next datagram: the lowest offset at or after the given one that the payload divisor and remainder allow.
	uint32 AlignPayload(uint32 offset) const {
		uint32 divisor = parameters.ndpInDivisor;
		uint32 remainder = parameters.ndpInPayloadRemainder;
		uint32 current = offset % divisor;
		if (current <= remainder) {
			return offset + (remainder - current);
		}

		return offset + (divisor - current + remainder);
	}


	// The offset of the NDP: the lowest multiple of the NDP alignment at or after the given offset.
	uint32 AlignNdp(uint32 offset) const {
		uint32 alignment = parameters.ndpInAlignment;
		return (offset + alignment - 1) / alignment * alignment;
	}


	// The size of an NDP16 that lists the given number of pairs, the terminator included: its header, four bytes a pair, and at least 16.
	static uint32 NdpSize(uint32 pairs) {
		uint32 size = USBNcmNdp16HeaderSize + pairs * USBNcmNdp16EntrySize;
		return std::max(size, USBNcmNdp16MinSize);
	}
};
