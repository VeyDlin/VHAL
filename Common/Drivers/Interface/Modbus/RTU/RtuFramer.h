#pragma once
#include <Utilities/Checksum/CRC/Engine/CrcTable.h>
#include <Utilities/Checksum/CRC/Preset/Crc16Modbus.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <cstddef>
#include <span>


// The RTU frame: the slave address, the PDU, and the CRC-16 sent low byte first. The framer builds a frame and checks a received one.
// It holds no state and no I/O.
class RtuFramer {
public:
	static constexpr std::size_t AddressSize = 1;
	static constexpr std::size_t CrcSize = 2;
	static constexpr std::size_t FrameOverhead = AddressSize + CrcSize;
	static constexpr std::size_t MinFrameSize = AddressSize + 1 + CrcSize;
	static constexpr std::size_t MaxPduSize = 253;
	static constexpr std::size_t MaxFrameSize = 256;
	static constexpr uint8 BroadcastAddress = 0;


	// The CRC-16 of data, the Modbus CRC-16 with the table engine: its 256-entry table is flash data built at compile time. The
	// value is sent low byte first.
	static constexpr uint16 Crc(std::span<const uint8> data) {
		return Checksum::Crc16Modbus::With<Checksum::CrcTable>::Calculate(data);
	}


	// The size of the frame that carries a PDU of pduSize bytes.
	static constexpr std::size_t FrameSize(std::size_t pduSize) {
		return pduSize + FrameOverhead;
	}


	// Writes the address, the PDU and the CRC into frame. Returns invalidParameter for an empty or too long PDU, or a frame that
	// is too small for it.
	static constexpr ResultStatus Build(std::span<uint8> frame, uint8 slaveAddress, std::span<const uint8> pdu) {
		if (pdu.empty() || pdu.size() > MaxPduSize || frame.size() < FrameSize(pdu.size())) {
			return ResultStatus::invalidParameter;
		}

		frame[0] = slaveAddress;

		for (std::size_t index = 0; index < pdu.size(); index++) {
			frame[AddressSize + index] = pdu[index];
		}

		std::size_t crcIndex = AddressSize + pdu.size();
		uint16 crc = Crc(frame.first(crcIndex));
		frame[crcIndex] = static_cast<uint8>(crc & 0xFF);
		frame[crcIndex + 1] = static_cast<uint8>(crc >> 8);
		return ResultStatus::ok;
	}


	// Checks a received frame. The CRC comes first (crcError), then the slave address (invalidData). A frame shorter than
	// MinFrameSize is invalidData. The caller passes the address it expects: its own, or BroadcastAddress for a broadcast.
	static constexpr ResultStatus Check(std::span<const uint8> frame, uint8 slaveAddress) {
		if (frame.size() < MinFrameSize) {
			return ResultStatus::invalidData;
		}

		std::size_t crcIndex = frame.size() - CrcSize;
		uint16 received = static_cast<uint16>(frame[crcIndex] | (frame[crcIndex + 1] << 8));
		if (Crc(frame.first(crcIndex)) != received) {
			return ResultStatus::crcError;
		}

		if (frame[0] != slaveAddress) {
			return ResultStatus::invalidData;
		}

		return ResultStatus::ok;
	}


	// The PDU of a frame: the bytes between the address and the CRC. Empty when the frame is shorter than MinFrameSize.
	static constexpr std::span<const uint8> Pdu(std::span<const uint8> frame) {
		if (frame.size() < MinFrameSize) {
			return {};
		}

		return frame.subspan(AddressSize, frame.size() - FrameOverhead);
	}
};
