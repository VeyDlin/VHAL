#pragma once

#include <TestHelpers.h>
#include <VHAL.h>
#include <Utilities/Bootloader/Loader/AESFLASHBootloader.h>
#include <Utilities/Crypto/AES/AES.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>


// Flash region used by the bootloader tests: 16 KB at a fixed base, the firmware header occupies its last 8 bytes.
constexpr uint32 kFlashBase = 0x08000000;
constexpr uint32 kFlashSize = 0x4000;
constexpr uint32 kHeaderAddress = kFlashBase + kFlashSize - 8;
constexpr uint32 kEraseStep = 0x1000;

// Wire values are written out literally: a changed constant in the library shows up as a failing test.
constexpr uint8 kCmdPing = 0x01;
constexpr uint8 kCmdGetInfo = 0x02;
constexpr uint8 kCmdErase = 0x10;
constexpr uint8 kCmdWrite = 0x11;
constexpr uint8 kCmdRead = 0x12;
constexpr uint8 kCmdVerify = 0x13;
constexpr uint8 kCmdFinalize = 0x14;
constexpr uint8 kCmdResetWritePos = 0x15;
constexpr uint8 kCmdStart = 0x20;
constexpr uint8 kCmdGetStatus = 0x30;
constexpr uint8 kCmdSetIv = 0x18;

constexpr uint8 kStatusSuccess = 0x00;
constexpr uint8 kStatusError = 0x01;
constexpr uint8 kStatusInvalidCommand = 0x02;
constexpr uint8 kStatusBusy = 0x04;
constexpr uint8 kStatusInvalidParameters = 0x03;
constexpr uint8 kStatusProtected = 0x06;
constexpr uint8 kStatusCryptoError = 0x07;
constexpr uint8 kStatusEndOfData = 0x09;
constexpr uint8 kStatusChecksumMismatch = 0x0A;


namespace Harness {


using TestHelpers::AsBytes;
using TestHelpers::ResetSystem;


// Reference CRC-32 (IEEE 802.3, reflected), the same parameters as Crc::CRC_32().
inline uint32 Crc32(std::span<const uint8> data) {
	uint32 crc = 0xFFFFFFFF;

	for (const uint8 byte : data) {
		crc ^= byte;

		for (int bit = 0; bit < 8; bit++) {
			if ((crc & 1) != 0) {
				crc = (crc >> 1) ^ 0xEDB88320;
			} else {
				crc = crc >> 1;
			}
		}
	}

	return crc ^ 0xFFFFFFFF;
}


inline std::vector<uint8> LittleEndian32(uint32 value) {
	std::vector<uint8> bytes;
	bytes.push_back(static_cast<uint8>(value));
	bytes.push_back(static_cast<uint8>(value >> 8));
	bytes.push_back(static_cast<uint8>(value >> 16));
	bytes.push_back(static_cast<uint8>(value >> 24));
	return bytes;
}


// FirmwareHeader on the wire: crc32 first, then size, both little-endian.
inline std::vector<uint8> FirmwareHeaderBytes(uint32 crc, uint32 size) {
	std::vector<uint8> header = LittleEndian32(crc);
	std::vector<uint8> sizeBytes = LittleEndian32(size);
	header.insert(header.end(), sizeBytes.begin(), sizeBytes.end());
	return header;
}


inline uint32 ReadLe32(const std::vector<uint8>& bytes, size_t offset) {
	return static_cast<uint32>(bytes[offset])
		| (static_cast<uint32>(bytes[offset + 1]) << 8)
		| (static_cast<uint32>(bytes[offset + 2]) << 16)
		| (static_cast<uint32>(bytes[offset + 3]) << 24);
}


// Deterministic, non-uniform test pattern (never all 0xFF, so erased flash cannot hide a missing write).
inline std::vector<uint8> MakeImage(size_t size, uint8 seed) {
	std::vector<uint8> image(size);

	for (size_t i = 0; i < size; i++) {
		image[i] = static_cast<uint8>(seed + i * 7 + (i >> 8) * 13);
	}

	return image;
}


inline std::vector<uint8> FlashRead(const FakeFlash& flash, uint32 address, size_t size) {
	size_t offset = address - kFlashBase;
	return std::vector<uint8>(flash.memory.begin() + offset, flash.memory.begin() + offset + size);
}


inline void FillFlash(FakeFlash& flash, uint8 value) {
	std::fill(flash.memory.begin(), flash.memory.end(), value);
}


// Command packet on the wire: AA seq cmd len data...
inline std::vector<uint8> BuildCommand(uint8 sequence, uint8 command, std::span<const uint8> data = {}) {
	std::vector<uint8> packet;
	packet.reserve(4 + data.size());
	packet.push_back(0xAA);
	packet.push_back(sequence);
	packet.push_back(command);
	packet.push_back(static_cast<uint8>(data.size()));
	packet.insert(packet.end(), data.begin(), data.end());
	return packet;
}


// Length byte 255 is above PacketDataMaxSize (240); the data bytes contain no 0xAA.
inline std::vector<uint8> OversizedPacket(uint8 sequence) {
	std::vector<uint8> packet = { 0xAA, sequence, kCmdPing, 0xFF };
	packet.resize(4 + 255, 0x00);
	return packet;
}


// Response packet on the wire: BB seq status len data...
struct Reply {
	bool valid = false;
	uint8 sequence = 0;
	uint8 status = 0xFF;
	std::vector<uint8> data;
};


inline Reply ParseReply(const std::vector<uint8>& packet) {
	Reply reply;

	if (packet.size() < 4 || packet[0] != 0xBB || packet.size() != 4 + static_cast<size_t>(packet[3])) {
		return reply;
	}

	reply.valid = true;
	reply.sequence = packet[1];
	reply.status = packet[2];
	reply.data.assign(packet.begin() + 4, packet.end());
	return reply;
}


// Records every packet the bootloader sends; busyResponses makes the next SendData calls return busy.
class MockCommunication : public ICommunication {
public:
	int busyResponses = 0;
	int busyCount = 0;
	bool readyToSend = true;
	std::vector<std::vector<uint8>> sentPackets;
	size_t nextUnread = 0;


	ResultStatus Initialize() override {
		return ResultStatus::ok;
	}


	ResultStatus SendData(std::span<const uint8> data) override {
		if (busyResponses > 0) {
			busyResponses--;
			busyCount++;
			return ResultStatus::busy;
		}

		sentPackets.emplace_back(data.begin(), data.end());
		return ResultStatus::ok;
	}


	bool IsReadyToSend() const override {
		return readyToSend;
	}


	bool IsConnected() const override {
		return true;
	}


	void SetDataReceivedCallback(DataReceivedCallback callback) override {
		receivedCallback = std::move(callback);
	}


	size_t GetMaxPacketSize() const override {
		return 240;
	}


	// Delivers bytes from the host to the bootloader, as the transport would.
	void Feed(std::span<const uint8> data) {
		if (receivedCallback) {
			receivedCallback(data);
		}
	}


	bool HasUnread() const {
		return nextUnread < sentPackets.size();
	}


	Reply TakeReply() {
		Reply reply = ParseReply(sentPackets[nextUnread]);
		nextUnread++;
		return reply;
	}


private:
	DataReceivedCallback receivedCallback;
};


// Drives a bootloader like a host: sends commands, pumps Process() until the reply arrives.
template<typename Bootloader>
class Client {
public:
	Client(Bootloader& bootloaderRef, MockCommunication& commRef) : bootloader(bootloaderRef), comm(commRef) { }


	void Send(uint8 sequence, uint8 command, std::span<const uint8> data) {
		std::vector<uint8> packet = BuildCommand(sequence, command, data);
		comm.Feed(packet);
		bootloader.Process();
	}


	Reply CallWithSequence(uint8 sequence, uint8 command, std::span<const uint8> data = {}) {
		Send(sequence, command, data);
		return WaitReply();
	}


	Reply Call(uint8 command, std::span<const uint8> data = {}) {
		return CallWithSequence(NextSequence(), command, data);
	}


	// Pumps Process() until a reply is available. Returns an invalid Reply if none arrives.
	Reply WaitReply(uint32 maxSteps = 100000) {
		for (uint32 step = 0; step < maxSteps; step++) {
			if (comm.HasUnread()) {
				return comm.TakeReply();
			}

			bootloader.Process();
		}

		return Reply{};
	}


	bool Erase() {
		Reply reply = Call(kCmdErase);
		return reply.valid && reply.status == kStatusSuccess;
	}


	// Sends the image as Write commands with the given chunk sizes; sequence numbers go firstSequence, +1, ...
	bool UploadChunks(std::span<const uint8> image, const std::vector<size_t>& sizes, uint8 firstSequence) {
		size_t offset = 0;

		for (size_t index = 0; index < sizes.size(); index++) {
			if (offset + sizes[index] > image.size()) {
				return false;
			}

			std::span<const uint8> chunk = image.subspan(offset, sizes[index]);
			Reply reply = CallWithSequence(static_cast<uint8>(firstSequence + index), kCmdWrite, chunk);

			if (!reply.valid || reply.status != kStatusSuccess) {
				return false;
			}

			offset += sizes[index];
		}

		return offset == image.size();
	}


	bool Upload(std::span<const uint8> image, size_t chunkSize, uint8 firstSequence) {
		std::vector<size_t> sizes;
		size_t remaining = image.size();

		while (remaining > 0) {
			size_t part = std::min(chunkSize, remaining);
			sizes.push_back(part);
			remaining -= part;
		}

		return UploadChunks(image, sizes, firstSequence);
	}


	Reply FinalizeWith(uint32 crc, uint32 size) {
		std::vector<uint8> header = FirmwareHeaderBytes(crc, size);
		return Call(kCmdFinalize, header);
	}


	Reply Finalize(std::span<const uint8> image) {
		return FinalizeWith(Crc32(image), static_cast<uint32>(image.size()));
	}


private:
	Bootloader& bootloader;
	MockCommunication& comm;
	uint8 sequenceCounter = 1;


	uint8 NextSequence() {
		uint8 sequence = sequenceCounter;
		sequenceCounter = static_cast<uint8>(sequenceCounter + 1);
		return sequence;
	}
};


// Test double for the abstract bootloader: no-op start, erase in 4 KB steps, optional forced erase status.
template<typename Base>
class TestBootloader : public Base {
public:
	uint32 startCount = 0;
	uint32 eraseIterations = 0;
	std::optional<ResultStatus> eraseStatusOverride;


	template<typename... Args>
	explicit TestBootloader(Args&&... args) : Base(std::forward<Args>(args)...) { }


protected:
	bool OnCheckBootloaderRequest() const override {
		return true;
	}


	bool OnStartApplication() override {
		startCount++;
		return true;
	}


	ResultStatus OnEraseIteration(uint32 address, uint32 endAddress, uint32 iteration, uint32& bytesErased) override {
		(void)iteration;
		eraseIterations++;

		if (eraseStatusOverride.has_value()) {
			bytesErased = 0;
			return *eraseStatusOverride;
		}

		uint32 chunk = std::min(kEraseStep, endAddress - address);
		this->flashAdapter.Erase(address, chunk);
		bytesErased = chunk;

		if (address + chunk >= endAddress) {
			return ResultStatus::ok;
		}

		return ResultStatus::inProgress;
	}
};


using PlainBootloader = TestBootloader<FLASHBootloader<>>;
using AesBootloader = TestBootloader<AESFLASHBootloader<AESKeySize::AES128>>;


// Exposes ReadPacket() so a test can check that it never writes past the CommandPacket object.
class ProbeBootloader : public PlainBootloader {
public:
	template<typename... Args>
	explicit ProbeBootloader(Args&&... args) : PlainBootloader(std::forward<Args>(args)...) { }


	bool ReadPacketProbe(CommandPacket& packet) {
		return ReadPacket(packet);
	}
};


// Decryption hook that always fails, for the CryptoError path.
class DecryptFailBootloader : public PlainBootloader {
public:
	template<typename... Args>
	explicit DecryptFailBootloader(Args&&... args) : PlainBootloader(std::forward<Args>(args)...) { }


protected:
	std::span<uint8> OnDecryptData(std::span<uint8> data) override {
		(void)data;
		return std::span<uint8>();
	}
};


struct GuardedPacket {
	PlainBootloader::CommandPacket packet;
	std::array<uint8, 64> guard;
};


// Flash, transport and bootloader wired together; constructor arguments after the flash go to the bootloader.
template<typename Bootloader>
struct Rig {
	FakeFlash flash;
	MockCommunication comm;
	Bootloader bootloader;
	Client<Bootloader> client;


	template<typename... Args>
	explicit Rig(Args&&... args)
		: flash(kFlashBase, kFlashSize)
		, comm()
		, bootloader(comm, flash, std::forward<Args>(args)...)
		, client(bootloader, comm) {
	}


	ResultStatus Init() {
		return bootloader.Initialize();
	}
};


// Independent AES-CTR reference: keystream block i is AES(IV + i) with a 128-bit big-endian counter.
inline void IncrementCounter(std::array<uint8, 16>& counter) {
	for (size_t i = counter.size(); i-- > 0;) {
		counter[i] = static_cast<uint8>(counter[i] + 1);

		if (counter[i] != 0) {
			return;
		}
	}
}


inline std::vector<uint8> CtrReference(const std::array<uint8, 16>& key, const std::array<uint8, 16>& iv, std::span<const uint8> input) {
	AES128 aes(key);
	std::vector<uint8> output(input.size());
	std::array<uint8, 16> counter = iv;

	for (size_t offset = 0; offset < input.size(); offset += 16) {
		std::array<uint8, 16> keystream = counter;
		aes.EncryptECB(keystream);

		for (size_t i = 0; i < 16 && offset + i < input.size(); i++) {
			output[offset + i] = static_cast<uint8>(input[offset + i] ^ keystream[i]);
		}

		IncrementCounter(counter);
	}

	return output;
}


// Independent AES-CBC encryption reference (input must be a multiple of 16 bytes).
inline std::vector<uint8> CbcEncryptReference(const std::array<uint8, 16>& key, const std::array<uint8, 16>& iv, std::span<const uint8> plain) {
	AES128 aes(key);
	std::vector<uint8> output(plain.size());
	std::array<uint8, 16> chain = iv;

	for (size_t offset = 0; offset < plain.size(); offset += 16) {
		std::array<uint8, 16> block{};

		for (size_t i = 0; i < 16; i++) {
			block[i] = static_cast<uint8>(plain[offset + i] ^ chain[i]);
		}

		aes.EncryptECB(block);
		std::copy(block.begin(), block.end(), output.begin() + offset);
		chain = block;
	}

	return output;
}


// FIPS-197 Appendix B key.
inline std::array<uint8, 16> TestKey() {
	return { 0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c };
}


inline std::array<uint8, 16> TestIv() {
	return { 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F };
}


// A second IV for the same key; its low counter bytes carry into the next byte within a long image.
inline std::array<uint8, 16> SecondIv() {
	return { 0xC0, 0xDE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFA };
}


}
