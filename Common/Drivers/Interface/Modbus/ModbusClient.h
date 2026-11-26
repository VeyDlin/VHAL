#pragma once
#include <Drivers/Interface/Modbus/ModbusBroadcast.h>
#include <Drivers/Interface/Modbus/ModbusResult.h>
#include <Drivers/Interface/Modbus/Pdu/ReadCoils.h>
#include <Drivers/Interface/Modbus/Pdu/ReadDiscreteInputs.h>
#include <Drivers/Interface/Modbus/Pdu/ReadHoldingRegisters.h>
#include <Drivers/Interface/Modbus/Pdu/ReadInputRegisters.h>
#include <Drivers/Interface/Modbus/Pdu/WriteMultipleCoils.h>
#include <Drivers/Interface/Modbus/Pdu/WriteMultipleRegisters.h>
#include <Drivers/Interface/Modbus/Pdu/WriteSingleCoil.h>
#include <Drivers/Interface/Modbus/Pdu/WriteSingleRegister.h>
#include <Drivers/Interface/Modbus/Transport.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <Utilities/CompileTime/TextLiteral.h>
#include <array>
#include <cstddef>
#include <span>
#include <type_traits>


// The lock of a client that is shared by tasks. Only the type matters: a client with no Lock() in its builder takes ModbusNoLock,
// and then it has no lock code at all.
struct ModbusNoLock {
};


// A lock with the pair the client calls around each request.
template<typename Lock>
concept ModbusLockable = requires(Lock& lock) {
	{ lock.Lock() };
	{ lock.Unlock() };
};


// The settings of a client, built in the captureless lambda that names the client:
//
//   using Plc = ModbusClient<Link, RtuFramer, +[] {
//       return ClientBuilder()
//           .SlaveAddress(1)
//           .Timeout(500)
//           .Retries(2);
//   }>;
//
// Rules, checked at compile time:
//   - the slave address is 1 to 247 (0 is the broadcast address, which has its own functions);
//   - the timeout is 1 to 4294967 ms, per attempt (the wait is passed to the transport in microseconds, 32 bits);
//   - Lock() adds the lock code: a client with Lock() is given a lock object in its constructor, and it locks around each request.
class ClientBuilder {
public:
	uint8 slaveAddress = 1;
	uint32 timeoutMs = 500;
	uint8 retries = 2;
	bool locked = false;


public:
	constexpr ClientBuilder SlaveAddress(uint8 address) const {
		ModbusRequire<TextLiteral{"slave address must be 1 to 247 (0 is broadcast: use Broadcast())"}>(address >= 1 && address <= 247);

		ClientBuilder next = *this;
		next.slaveAddress = address;
		return next;
	}


	constexpr ClientBuilder Timeout(uint32 milliseconds) const {
		ModbusRequire<TextLiteral{"timeout must be above 0 ms"}>(milliseconds > 0);
		ModbusRequire<TextLiteral{"timeout must be at most 4294967 ms (the wait is in microseconds, 32 bits)"}>(milliseconds <= 4294967);

		ClientBuilder next = *this;
		next.timeoutMs = milliseconds;
		return next;
	}


	constexpr ClientBuilder Retries(uint8 count) const {
		ClientBuilder next = *this;
		next.retries = count;
		return next;
	}


	constexpr ClientBuilder Lock() const {
		ClientBuilder next = *this;
		next.locked = true;
		return next;
	}
};


// A Modbus client over a transport (ModbusTransport) and a framer (RtuFramer). Make is a captureless lambda that returns the
// ClientBuilder. Each request is one transaction: it sends the frame, waits for one reply, and checks it.
//
// Retries: a timeout, a CRC error, or a reply that breaks a check (invalidData) sends the request again, up to the builder's count.
// An exception reply, invalidParameter, and a transport failure are not retried. A timed-out write may have reached the device, so
// a retry can apply it twice. Use Retries(0) for a pulse or a counter.
//
// Read functions fill the caller's values. With a std::array the quantity is checked at compile time; with a span it is checked
// when the call runs. The client is not reentrant unless it is locked.
template<typename Transport, typename Framer, auto (*Make)(), typename LockType = ModbusNoLock>
class ModbusClient {
public:
	static constexpr ClientBuilder settings = Make();
	static_assert(settings.locked == !std::is_same_v<LockType, ModbusNoLock>, "Lock() in the builder needs a lock type, and a lock type needs Lock() in the builder");
	static_assert(!settings.locked || ModbusLockable<LockType>, "the lock type needs Lock() and Unlock()");
	static_assert(ModbusTransport<Transport>, "the transport needs Begin, Send and Receive");


	friend class ModbusBroadcast<ModbusClient>;


private:
	Transport* link;
	LockType* lock;
	std::array<uint8, Framer::MaxFrameSize> txFrame{};
	std::array<uint8, Framer::MaxFrameSize> rxFrame{};


public:
	// The lock is used only when the builder has Lock(). A locked client needs a lock object: a request on a locked client with no lock
	// object returns noInit and sends nothing. The constructor does not check it.
	explicit ModbusClient(Transport& transport, LockType* guard = nullptr) : link(&transport), lock(guard) {
	}


	ModbusBroadcast<ModbusClient> Broadcast() {
		return ModbusBroadcast<ModbusClient>(*this);
	}


	// Reads coils (01) and discrete inputs (02) into bools, one per coil.
	ModbusResult ReadCoils(uint16 address, std::span<bool> values) {
		return ReadBitsOf<ReadCoilsPdu>(address, values);
	}


	template<std::size_t Count>
	ModbusResult ReadCoils(uint16 address, std::array<bool, Count>& values) {
		ReadCoilsPdu::RequireCount<Count>();
		return ReadBitsOf<ReadCoilsPdu>(address, std::span<bool>(values));
	}


	ModbusResult ReadDiscreteInputs(uint16 address, std::span<bool> values) {
		return ReadBitsOf<ReadDiscreteInputsPdu>(address, values);
	}


	template<std::size_t Count>
	ModbusResult ReadDiscreteInputs(uint16 address, std::array<bool, Count>& values) {
		ReadDiscreteInputsPdu::RequireCount<Count>();
		return ReadBitsOf<ReadDiscreteInputsPdu>(address, std::span<bool>(values));
	}


	// Reads holding registers (03) and input registers (04).
	ModbusResult ReadHoldingRegisters(uint16 address, std::span<uint16> values) {
		return ReadWordsOf<ReadHoldingRegistersPdu>(address, values);
	}


	template<std::size_t Count>
	ModbusResult ReadHoldingRegisters(uint16 address, std::array<uint16, Count>& values) {
		ReadHoldingRegistersPdu::RequireCount<Count>();
		return ReadWordsOf<ReadHoldingRegistersPdu>(address, std::span<uint16>(values));
	}


	ModbusResult ReadInputRegisters(uint16 address, std::span<uint16> values) {
		return ReadWordsOf<ReadInputRegistersPdu>(address, values);
	}


	template<std::size_t Count>
	ModbusResult ReadInputRegisters(uint16 address, std::array<uint16, Count>& values) {
		ReadInputRegistersPdu::RequireCount<Count>();
		return ReadWordsOf<ReadInputRegistersPdu>(address, std::span<uint16>(values));
	}


	ModbusResult WriteSingleCoil(uint16 address, bool on) {
		return Guarded([&]() -> ModbusResult {
			ResultStatus built = WriteSingleCoilPdu::BuildRequest(RequestPdu(), address, on);
			if (built != ResultStatus::ok) {
				return ModbusResult{built, 0};
			}

			return Exchange(WriteSingleCoilPdu::RequestSize, [&](std::span<const uint8> reply) -> ModbusResult {
				return WriteSingleCoilPdu::CheckReply(RequestOf(WriteSingleCoilPdu::RequestSize), reply);
			});
		});
	}


	ModbusResult WriteSingleRegister(uint16 address, uint16 value) {
		return Guarded([&]() -> ModbusResult {
			ResultStatus built = WriteSingleRegisterPdu::BuildRequest(RequestPdu(), address, value);
			if (built != ResultStatus::ok) {
				return ModbusResult{built, 0};
			}

			return Exchange(WriteSingleRegisterPdu::RequestSize, [&](std::span<const uint8> reply) -> ModbusResult {
				return WriteSingleRegisterPdu::CheckReply(RequestOf(WriteSingleRegisterPdu::RequestSize), reply);
			});
		});
	}


	ModbusResult WriteMultipleCoils(uint16 address, std::span<const bool> values) {
		return Guarded([&]() -> ModbusResult {
			if (values.empty() || values.size() > WriteMultipleCoilsPdu::MaxQuantity) {
				return ModbusResult{ResultStatus::invalidParameter, 0};
			}

			std::size_t size = WriteMultipleCoilsPdu::RequestSize(values.size());
			ResultStatus built = WriteMultipleCoilsPdu::BuildRequest(RequestPdu(), address, values);
			if (built != ResultStatus::ok) {
				return ModbusResult{built, 0};
			}

			return Exchange(size, [&](std::span<const uint8> reply) -> ModbusResult {
				return WriteMultipleCoilsPdu::CheckReply(RequestOf(size), reply);
			});
		});
	}


	ModbusResult WriteMultipleRegisters(uint16 address, std::span<const uint16> values) {
		return Guarded([&]() -> ModbusResult {
			if (values.empty() || values.size() > WriteMultipleRegistersPdu::MaxQuantity) {
				return ModbusResult{ResultStatus::invalidParameter, 0};
			}

			std::size_t size = WriteMultipleRegistersPdu::RequestSize(values.size());
			ResultStatus built = WriteMultipleRegistersPdu::BuildRequest(RequestPdu(), address, values);
			if (built != ResultStatus::ok) {
				return ModbusResult{built, 0};
			}

			return Exchange(size, [&](std::span<const uint8> reply) -> ModbusResult {
				return WriteMultipleRegistersPdu::CheckReply(RequestOf(size), reply);
			});
		});
	}


private:
	// The PDU area of the transmit frame: the frame starts with the address, so the PDU starts at index 1.
	std::span<uint8> RequestPdu() {
		return std::span<uint8>(txFrame).subspan(1);
	}


	std::span<const uint8> RequestOf(std::size_t size) const {
		return std::span<const uint8>(txFrame).subspan(1, size);
	}


	// Runs operation under the lock when the builder asks for one. Without Lock() the call is a plain call. A locked client with no
	// lock object gives noInit, so no request is sent without the lock.
	template<typename Operation>
	ModbusResult Guarded(Operation operation) {
		if constexpr (settings.locked) {
			if (lock == nullptr) {
				return ModbusResult{ResultStatus::noInit, 0};
			}

			lock->Lock();
			ModbusResult result = operation();
			lock->Unlock();
			return result;
		} else {
			return operation();
		}
	}


	// The time one attempt waits: the transport's send wait and its receive wait are both the timeout, in microseconds.
	uint32 ReplyWaitUs() const {
		return settings.timeoutMs * 1000;
	}


	// Writes the address and the CRC around the PDU already in the transmit frame. The PDU is copied onto itself, so it stays in
	// place. Returns the number of bytes to send.
	std::size_t Seal(std::size_t pduSize, uint8 address) {
		Framer::Build(txFrame, address, RequestOf(pduSize));
		return Framer::FrameSize(pduSize);
	}


	// Sends the request in the transmit frame and waits for one reply that the check accepts. The check gets the reply PDU after the
	// framer has checked the CRC and the address; it returns the outcome of the request. Retries as the file comment says.
	template<typename Check>
	ModbusResult Exchange(std::size_t pduSize, Check check) {
		std::size_t frameSize = Seal(pduSize, settings.slaveAddress);
		std::span<const uint8> frame(txFrame.data(), frameSize);
		ModbusResult last{ResultStatus::timeout, 0};

		for (uint16 attempt = 0; attempt <= settings.retries; attempt++) {
			ResultStatus sent = link->Send(frame, ReplyWaitUs());
			if (sent == ResultStatus::timeout) {
				last = ModbusResult{ResultStatus::timeout, 0};
				continue;
			}

			if (sent != ResultStatus::ok) {
				return ModbusResult{ResultStatus::writeError, 0};
			}

			Result<uint16> received = link->Receive(rxFrame, settings.timeoutMs * 1000);
			if (received.IsErr()) {
				ResultStatus failure = received.Error();
				if (failure != ResultStatus::timeout && failure != ResultStatus::invalidData) {
					return ModbusResult{ResultStatus::readError, 0};
				}

				last = ModbusResult{failure, 0};
				continue;
			}

			std::span<const uint8> reply(rxFrame.data(), received.Value());
			ResultStatus framed = Framer::Check(reply, settings.slaveAddress);
			if (framed != ResultStatus::ok) {
				last = ModbusResult{framed, 0};
				continue;
			}

			ModbusResult checked = check(Framer::Pdu(reply));
			if (checked.status == ResultStatus::ok || checked.status == ResultStatus::error) {
				return checked;
			}

			last = checked;
		}

		return last;
	}


	// One read of bits (01 or 02). The quantity is the size of values, checked before the request is built.
	template<typename Pdu>
	ModbusResult ReadBitsOf(uint16 address, std::span<bool> values) {
		return Guarded([&]() -> ModbusResult {
			if (values.empty() || values.size() > Pdu::MaxQuantity) {
				return ModbusResult{ResultStatus::invalidParameter, 0};
			}

			ResultStatus built = Pdu::BuildRequest(RequestPdu(), address, static_cast<uint16>(values.size()));
			if (built != ResultStatus::ok) {
				return ModbusResult{built, 0};
			}

			return Exchange(Pdu::RequestSize, [&](std::span<const uint8> reply) -> ModbusResult {
				ModbusResult checked = Pdu::CheckReply(RequestOf(Pdu::RequestSize), reply);
				if (checked.status == ResultStatus::ok) {
					Pdu::ReadBits(reply, values);
				}
				return checked;
			});
		});
	}


	// One read of registers (03 or 04). The quantity is the size of values, checked before the request is built.
	template<typename Pdu>
	ModbusResult ReadWordsOf(uint16 address, std::span<uint16> values) {
		return Guarded([&]() -> ModbusResult {
			if (values.empty() || values.size() > Pdu::MaxQuantity) {
				return ModbusResult{ResultStatus::invalidParameter, 0};
			}

			ResultStatus built = Pdu::BuildRequest(RequestPdu(), address, static_cast<uint16>(values.size()));
			if (built != ResultStatus::ok) {
				return ModbusResult{built, 0};
			}

			return Exchange(Pdu::RequestSize, [&](std::span<const uint8> reply) -> ModbusResult {
				ModbusResult checked = Pdu::CheckReply(RequestOf(Pdu::RequestSize), reply);
				if (checked.status == ResultStatus::ok) {
					Pdu::ReadValues(reply, values);
				}
				return checked;
			});
		});
	}


	// The shared send of a broadcast: the frame goes to address 0 and no reply is read. build writes the PDU and returns its status.
	template<typename Build>
	ModbusResult SendBroadcast(std::size_t pduSize, Build build) {
		return Guarded([&]() -> ModbusResult {
			ResultStatus built = build(RequestPdu());
			if (built != ResultStatus::ok) {
				return ModbusResult{built, 0};
			}

			std::size_t frameSize = Seal(pduSize, Framer::BroadcastAddress);
			ResultStatus sent = link->Send(std::span<const uint8>(txFrame.data(), frameSize), ReplyWaitUs());
			if (sent == ResultStatus::ok) {
				return ModbusResult{ResultStatus::ok, 0};
			}

			if (sent == ResultStatus::timeout) {
				return ModbusResult{ResultStatus::timeout, 0};
			}

			return ModbusResult{ResultStatus::writeError, 0};
		});
	}
};
