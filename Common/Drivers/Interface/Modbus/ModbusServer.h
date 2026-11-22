#pragma once
#include <Drivers/Interface/Modbus/ModbusResult.h>
#include <Drivers/Interface/Modbus/Pdu/PduCommon.h>
#include <Drivers/Interface/Modbus/Pdu/ReadCoils.h>
#include <Drivers/Interface/Modbus/Pdu/ReadDiscreteInputs.h>
#include <Drivers/Interface/Modbus/Pdu/ReadHoldingRegisters.h>
#include <Drivers/Interface/Modbus/Pdu/ReadInputRegisters.h>
#include <Drivers/Interface/Modbus/Pdu/WriteMultipleCoils.h>
#include <Drivers/Interface/Modbus/Pdu/WriteMultipleRegisters.h>
#include <Drivers/Interface/Modbus/Pdu/WriteSingleCoil.h>
#include <Drivers/Interface/Modbus/Pdu/WriteSingleRegister.h>
#include <Drivers/Interface/Modbus/Transport.h>
#include <Utilities/Data/RegisterMap/RegisterBus.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <Utilities/TextLiteral.h>
#include <array>
#include <cstddef>
#include <span>


// The settings of a server, built in the captureless lambda that names the server:
//
//   using Server = ModbusServer<Link, RtuFramer, Table, +[] {
//       return ServerBuilder()
//           .SlaveAddress(1);
//   }>;
//
// Rules, checked at compile time: the slave address is 1 to 247. MaxCoilWrite sets the most coils one 0F request may write; the
// server keeps that many bools in RAM when its view has coils, so a smaller limit saves RAM. Requests above it get exception 03.
class ServerBuilder {
public:
	uint8 slaveAddress = 1;
	uint16 maxCoilWrite = 1968;


public:
	constexpr ServerBuilder SlaveAddress(uint8 address) const {
		ModbusRequire<TextLiteral{"slave address must be 1 to 247 (0 is broadcast: use Broadcast())"}>(address >= 1 && address <= 247);

		ServerBuilder next = *this;
		next.slaveAddress = address;
		return next;
	}


	constexpr ServerBuilder MaxCoilWrite(uint16 count) const {
		ModbusRequire<TextLiteral{"a coil write takes 1 to 1968 coils"}>(count >= 1 && count <= 1968);

		ServerBuilder next = *this;
		next.maxCoilWrite = count;
		return next;
	}
};


// The request of a read is address and quantity, five bytes with the function code.
constexpr std::size_t ModbusReadRequestSize = 5;


// True when the view has an entry in the table. The server instantiates a function code only for the tables its view has.
template<typename View>
constexpr bool ModbusViewHasTable(RegisterTable table) {
	for (std::size_t index = 0; index < View::slotCount; index++) {
		if (View::slots[index].table == table) {
			return true;
		}
	}

	return false;
}


// A Modbus server over a transport and a framer, serving the entries of a RegisterView. The server owns no data: the view points at
// the user's variables. Process() takes at most one frame per call and answers it.
//
// The function codes are the ones the view's entry kinds need: holding registers enable 03, 06 and 10; input registers 04; coils
// 01, 05 and 0F; discrete inputs 02. Any other code, and a code whose table the view does not have, gets exception 01.
//
// A write is all or nothing. The view checks the whole request before it writes, so a range outside the view, a read-only entry
// or a rejected value (02, 02 or 03) changes nothing. A set function that fails in the middle of a request gives exception 04, and
// the entries before it stay written.
//
// Broadcasts (address 0) run the write codes only and get no reply. A frame for another slave gets no reply.
template<typename Transport, typename Framer, typename View, auto (*Make)()>
class ModbusServer {
public:
	static constexpr ServerBuilder settings = Make();
	static_assert(View::checked, "the view entries were not checked");
	static_assert(ModbusTransport<Transport>, "the transport needs Begin, Send and Receive");


	static constexpr bool hasHolding = ModbusViewHasTable<View>(RegisterTable::Holding);
	static constexpr bool hasInput = ModbusViewHasTable<View>(RegisterTable::Input);
	static constexpr bool hasCoils = ModbusViewHasTable<View>(RegisterTable::Coil);
	static constexpr bool hasDiscrete = ModbusViewHasTable<View>(RegisterTable::Discrete);


private:
	Transport* link;
	std::array<uint8, Framer::MaxFrameSize> rxFrame{};
	std::array<uint8, Framer::MaxFrameSize> txFrame{};
	std::array<bool, hasCoils ? settings.maxCoilWrite : 0> coilBuffer{};
	std::array<uint16, hasHolding || hasInput ? ReadHoldingRegistersPdu::MaxQuantity : 0> wordBuffer{};


public:
	explicit ModbusServer(Transport& transport) : link(&transport) {
	}


	// Takes one complete frame from the transport and answers it. Returns timeout when no frame is ready, invalidData or crcError
	// for a frame that broke, and ok after a frame is handled, with or without a reply. A send failure returns writeError.
	ResultStatus Process() {
		Result<uint16> received = link->Receive(rxFrame, 0);
		if (received.IsErr()) {
			return received.Error();
		}

		std::span<const uint8> frame(rxFrame.data(), received.Value());
		if (frame.size() < Framer::MinFrameSize) {
			return ResultStatus::invalidData;
		}

		uint8 address = frame[0];
		ResultStatus framed = Framer::Check(frame, address);
		if (framed != ResultStatus::ok) {
			return framed;
		}

		if (address != settings.slaveAddress && address != Framer::BroadcastAddress) {
			return ResultStatus::ok;
		}

		bool broadcast = address == Framer::BroadcastAddress;
		std::span<const uint8> pdu = Framer::Pdu(frame);
		if (broadcast && !IsWriteCode(pdu[0])) {
			return ResultStatus::ok;
		}

		std::size_t replySize = Execute(pdu, ReplyPdu());
		if (broadcast) {
			return ResultStatus::ok;
		}

		Framer::Build(txFrame, settings.slaveAddress, std::span<const uint8>(txFrame).subspan(1, replySize));
		ResultStatus sent = link->Send(std::span<const uint8>(txFrame).first(Framer::FrameSize(replySize)), ModbusReplyWaitUs);
		return sent == ResultStatus::ok ? ResultStatus::ok : ResultStatus::writeError;
	}


private:
	std::span<uint8> ReplyPdu() {
		return std::span<uint8>(txFrame).subspan(1);
	}


	static bool IsWriteCode(uint8 code) {
		return code == WriteSingleCoilPdu::Code || code == WriteSingleRegisterPdu::Code || code == WriteMultipleCoilsPdu::Code
			|| code == WriteMultipleRegistersPdu::Code;
	}


	// Writes an exception reply into reply and returns its length.
	static std::size_t Fail(uint8 code, std::span<uint8> reply, uint8 exception) {
		reply[0] = static_cast<uint8>(code | ModbusExceptionFlag);
		reply[1] = exception;
		return ModbusExceptionPduSize;
	}


	// Copies the request into the reply: the echo of the write functions (05, 06, 0F and 10 reply with the first five bytes).
	static std::size_t Echo(std::span<const uint8> pdu, std::span<uint8> reply) {
		for (std::size_t index = 0; index < ModbusEchoPduSize; index++) {
			reply[index] = pdu[index];
		}

		return ModbusEchoPduSize;
	}


	// The exception for a failed view call: a range or access error is 02, a rejected value 03, and a failed set 04.
	static uint8 ExceptionOf(ResultStatus status) {
		if (status == ResultStatus::badAddress || status == ResultStatus::accessError) {
			return ModbusExceptionIllegalDataAddress;
		}

		if (status == ResultStatus::invalidData) {
			return ModbusExceptionIllegalDataValue;
		}

		return ModbusExceptionServerFailure;
	}


	// Runs one request and writes its reply PDU into reply. Returns the reply length. Codes the view does not enable get exception 01.
	std::size_t Execute(std::span<const uint8> pdu, std::span<uint8> reply) {
		switch (pdu[0]) {
			case ReadCoilsPdu::Code:
				if constexpr (hasCoils) {
					return ReadBitsRequest<RegisterTable::Coil, ReadCoilsPdu::MaxQuantity>(pdu, reply);
				}
				break;

			case ReadDiscreteInputsPdu::Code:
				if constexpr (hasDiscrete) {
					return ReadBitsRequest<RegisterTable::Discrete, ReadDiscreteInputsPdu::MaxQuantity>(pdu, reply);
				}
				break;

			case ReadHoldingRegistersPdu::Code:
				if constexpr (hasHolding) {
					return ReadWordsRequest<RegisterTable::Holding, ReadHoldingRegistersPdu::MaxQuantity>(pdu, reply);
				}
				break;

			case ReadInputRegistersPdu::Code:
				if constexpr (hasInput) {
					return ReadWordsRequest<RegisterTable::Input, ReadInputRegistersPdu::MaxQuantity>(pdu, reply);
				}
				break;

			case WriteSingleCoilPdu::Code:
				if constexpr (hasCoils) {
					return WriteSingleCoilRequest(pdu, reply);
				}
				break;

			case WriteSingleRegisterPdu::Code:
				if constexpr (hasHolding) {
					return WriteSingleRegisterRequest(pdu, reply);
				}
				break;

			case WriteMultipleCoilsPdu::Code:
				if constexpr (hasCoils) {
					return WriteCoilsRequest(pdu, reply);
				}
				break;

			case WriteMultipleRegistersPdu::Code:
				if constexpr (hasHolding) {
					return WriteRegistersRequest(pdu, reply);
				}
				break;

			default:
				break;
		}

		return Fail(pdu[0], reply, ModbusExceptionIllegalFunction);
	}


	// Reads holding or input registers (03 or 04). The words are read in one call, so each entry is read once and its words come from
	// one snapshot. A read failure gives exception 02.
	template<RegisterTable Table, std::size_t Limit>
	std::size_t ReadWordsRequest(std::span<const uint8> pdu, std::span<uint8> reply) {
		uint8 code = pdu[0];
		if (pdu.size() != ModbusReadRequestSize) {
			return Fail(code, reply, ModbusExceptionIllegalDataValue);
		}

		uint16 address = PduGetU16(pdu, 1);
		std::size_t quantity = PduGetU16(pdu, 3);
		if (quantity < 1 || quantity > Limit) {
			return Fail(code, reply, ModbusExceptionIllegalDataValue);
		}

		if (static_cast<std::size_t>(address) + quantity > 0x10000) {
			return Fail(code, reply, ModbusExceptionIllegalDataAddress);
		}

		ResultStatus status = View::ReadWords(Table, address, std::span<uint16>(wordBuffer.data(), quantity));
		if (status != ResultStatus::ok) {
			return Fail(code, reply, ExceptionOf(status));
		}

		reply[0] = code;
		reply[1] = static_cast<uint8>(2 * quantity);
		for (std::size_t index = 0; index < quantity; index++) {
			PduPutU16(reply, ModbusReadHeaderSize + 2 * index, wordBuffer[index]);
		}

		return ModbusReadHeaderSize + 2 * quantity;
	}


	// Reads coils or discrete inputs (01 or 02). The view packs the bits straight into the reply, least significant first, and reads
	// each entry once, so a computed coil runs once per request. The reply holds at most 250 bytes of bits, which fits the frame.
	template<RegisterTable Table, std::size_t Limit>
	std::size_t ReadBitsRequest(std::span<const uint8> pdu, std::span<uint8> reply) {
		uint8 code = pdu[0];
		if (pdu.size() != ModbusReadRequestSize) {
			return Fail(code, reply, ModbusExceptionIllegalDataValue);
		}

		uint16 address = PduGetU16(pdu, 1);
		std::size_t quantity = PduGetU16(pdu, 3);
		if (quantity < 1 || quantity > Limit) {
			return Fail(code, reply, ModbusExceptionIllegalDataValue);
		}

		if (static_cast<std::size_t>(address) + quantity > 0x10000) {
			return Fail(code, reply, ModbusExceptionIllegalDataAddress);
		}

		std::size_t byteCount = PduBitBytes(quantity);
		ResultStatus status = View::ReadPackedBits(Table, address, quantity, reply.subspan(ModbusReadHeaderSize, byteCount));
		if (status != ResultStatus::ok) {
			return Fail(code, reply, ExceptionOf(status));
		}

		reply[0] = code;
		reply[1] = static_cast<uint8>(byteCount);
		return ModbusReadHeaderSize + byteCount;
	}


	// Writes one coil (05). Only 0xFF00 (on) and 0x0000 (off) are valid values.
	std::size_t WriteSingleCoilRequest(std::span<const uint8> pdu, std::span<uint8> reply) {
		uint8 code = pdu[0];
		if (pdu.size() != ModbusEchoPduSize) {
			return Fail(code, reply, ModbusExceptionIllegalDataValue);
		}

		uint16 value = PduGetU16(pdu, 3);
		if (value != WriteSingleCoilPdu::OnValue && value != WriteSingleCoilPdu::OffValue) {
			return Fail(code, reply, ModbusExceptionIllegalDataValue);
		}

		std::array<bool, 1> bits{value == WriteSingleCoilPdu::OnValue};
		ResultStatus status = View::WriteBits(RegisterTable::Coil, PduGetU16(pdu, 1), std::span<const bool>(bits));
		if (status != ResultStatus::ok) {
			return Fail(code, reply, ExceptionOf(status));
		}

		return Echo(pdu, reply);
	}


	// Writes one register (06).
	std::size_t WriteSingleRegisterRequest(std::span<const uint8> pdu, std::span<uint8> reply) {
		uint8 code = pdu[0];
		if (pdu.size() != ModbusEchoPduSize) {
			return Fail(code, reply, ModbusExceptionIllegalDataValue);
		}

		std::array<uint16, 1> words{PduGetU16(pdu, 3)};
		ResultStatus status = View::WriteWords(RegisterTable::Holding, PduGetU16(pdu, 1), std::span<const uint16>(words));
		if (status != ResultStatus::ok) {
			return Fail(code, reply, ExceptionOf(status));
		}

		return Echo(pdu, reply);
	}


	// Writes several coils (0F). The request is checked as a whole before the view writes any coil.
	std::size_t WriteCoilsRequest(std::span<const uint8> pdu, std::span<uint8> reply) {
		uint8 code = pdu[0];
		if (pdu.size() < WriteMultipleCoilsPdu::RequestHeaderSize) {
			return Fail(code, reply, ModbusExceptionIllegalDataValue);
		}

		uint16 address = PduGetU16(pdu, 1);
		std::size_t quantity = PduGetU16(pdu, 3);
		std::size_t byteCount = pdu[5];
		if (quantity < 1 || quantity > WriteMultipleCoilsPdu::MaxQuantity || quantity > settings.maxCoilWrite) {
			return Fail(code, reply, ModbusExceptionIllegalDataValue);
		}

		if (byteCount != PduBitBytes(quantity) || pdu.size() != WriteMultipleCoilsPdu::RequestHeaderSize + byteCount) {
			return Fail(code, reply, ModbusExceptionIllegalDataValue);
		}

		for (std::size_t index = 0; index < quantity; index++) {
			coilBuffer[index] = ((pdu[WriteMultipleCoilsPdu::RequestHeaderSize + index / 8] >> (index % 8)) & 1) != 0;
		}

		ResultStatus status = View::WriteBits(RegisterTable::Coil, address, std::span<const bool>(coilBuffer.data(), quantity));
		if (status != ResultStatus::ok) {
			return Fail(code, reply, ExceptionOf(status));
		}

		return Echo(pdu, reply);
	}


	// Writes several registers (10). The request is checked as a whole before the view writes any register.
	std::size_t WriteRegistersRequest(std::span<const uint8> pdu, std::span<uint8> reply) {
		uint8 code = pdu[0];
		if (pdu.size() < WriteMultipleRegistersPdu::RequestHeaderSize) {
			return Fail(code, reply, ModbusExceptionIllegalDataValue);
		}

		uint16 address = PduGetU16(pdu, 1);
		std::size_t quantity = PduGetU16(pdu, 3);
		std::size_t byteCount = pdu[5];
		if (quantity < 1 || quantity > WriteMultipleRegistersPdu::MaxQuantity) {
			return Fail(code, reply, ModbusExceptionIllegalDataValue);
		}

		if (byteCount != 2 * quantity || pdu.size() != WriteMultipleRegistersPdu::RequestHeaderSize + byteCount) {
			return Fail(code, reply, ModbusExceptionIllegalDataValue);
		}

		for (std::size_t index = 0; index < quantity; index++) {
			wordBuffer[index] = PduGetU16(pdu, WriteMultipleRegistersPdu::RequestHeaderSize + 2 * index);
		}

		ResultStatus status = View::WriteWords(RegisterTable::Holding, address, std::span<const uint16>(wordBuffer.data(), quantity));
		if (status != ResultStatus::ok) {
			return Fail(code, reply, ExceptionOf(status));
		}

		return Echo(pdu, reply);
	}
};
