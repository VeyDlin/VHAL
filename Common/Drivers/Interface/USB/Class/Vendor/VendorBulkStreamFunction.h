#pragma once
#include <Drivers/Interface/USB/Class/Vendor/VendorBulkFunctionBase.h>
#include <Utilities/Callback/Delegate.h>
#include <array>
#include <span>


// Streaming mode of the vendor bulk function, the throughput path. The application supplies a producer that fills an IN
// transfer buffer and a consumer that takes the bytes of each received OUT packet. The consumer runs under a critical section,
// called from OnTransferComplete in interrupt context, or from Process() when the port completes an OUT transfer inside the arm.
// It must be safe in both places. The endpoints are re-armed at once, so the adapter's double buffering stays fed.
//
// The consumer is called once per OUT packet, with 1 to 64 bytes: each OUT transfer is one packet, so a host write of N packets
// gives N calls, and the last call of a write that ends on a packet boundary is a full packet. The span is valid only during the
// call: the function keeps no copy of it.
// The consumer must take every packet. It runs in interrupt context, once per OUT packet, and the function has no backpressure:
// the endpoint is re-armed as soon as the consumer returns, so a packet the consumer does not keep is lost. The consumer copies
// the bytes or handles them before it returns. The bench takes every packet in its own StoreOut() override, which checks or loops
// each one, so it sets no consumer.
// Process() starts the first IN transfer, arms the OUT endpoint while CanAcceptOut() allows it (by default while a consumer is
// set; the bench overrides it), and fills the second IN stage
// while the first one is in flight. So the application must call Process() from its loop.
//
// The producer runs in Process() when a stage is free, so it is not called at the end of every IN transfer. It runs in
// interrupt context only when no stage is filled and the interrupt has to start one (a function that never calls Process()
// after its first start). Keep it short either way.
//
// The producer and consumer are set with SetProducer() and SetConsumer() before the device starts. They are refused (busy)
// while the function is configured, because the callables are called from the device's context.
//
// Bytes are not kept across an abandoned transfer. When a host halts the IN endpoint or a start fails, the bytes the
// producer handed out for the stages in flight and waiting are not sent again, and the producer has already moved past
// them. The host then sees a gap in the producer's sequence after ClearStall. A producer that needs an unbroken sequence
// must resynchronise itself after a halt; the function does not replay the bytes.
//
// PacketsPerTransfer is the length of each transfer buffer in packets. IN has InStageCount such buffers, so the RAM for IN is
// InStageCount * PacketsPerTransfer * 64 bytes. OUT is one packet (64 bytes), whatever PacketsPerTransfer is.
template<uint8 PacketsPerTransfer = 8>
class VendorBulkStreamFunction : public VendorBulkFunctionBase {
	static_assert(PacketsPerTransfer > 0, "A transfer buffer holds at least one packet");


public:
	// Fills packets with bytes and returns how many, at most packets.size(). A count that is not a multiple of PacketSize ends
	// the host's IN transfer with a short packet, so only the last packet of a stream may be short. A whole number of packets
	// gets no zero-length packet: the host reads the length it asked for (the bench protocol), and a zero-length packet would end
	// that read early. The stage just completes, and the next stage goes out when the producer has bytes for it. A producer
	// whose protocol needs a terminator sends a short packet itself, as its last bytes.
	// A delegate, not std::function: it holds a function pointer and a context pointer, so it never allocates. The context is the
	// application's object, which must outlive the function.
	using Producer = Callback::Delegate<size_t(std::span<uint8> packets)>;


	// Takes the bytes of one received OUT packet (1 to 64 bytes). The span is valid only during the call. Same delegate as the producer.
	using Consumer = Callback::Delegate<void(std::span<const uint8> packets)>;


	static constexpr size_t TransferSize = static_cast<size_t>(PacketsPerTransfer) * PacketSize;


private:
	Producer producer;
	Consumer consumer;
	std::array<std::array<uint8, TransferSize>, InStageCount> inBuffers{};
	// One packet: an OUT transfer is one packet, so the consumer gets each packet as it arrives.
	std::array<uint8, PacketSize> outBuffer{};


public:
	VendorBulkStreamFunction(std::span<const uint8> interfaces, std::span<const uint8> endpoints)
		: VendorBulkFunctionBase(interfaces, endpoints) {
		std::array<std::span<uint8>, InStageCount> stages{};
		for (uint8 index = 0; index < InStageCount; index++) {
			stages[index] = inBuffers[index];
		}

		SetBulkStages(stages, outBuffer);
	}


	// From the fluent builder's layout (USBDescriptors::Function()).
	explicit VendorBulkStreamFunction(const USBFunctionLayout& layout)
		: VendorBulkStreamFunction(layout.interfaces, layout.endpoints) {
	}


	// Sets the producer. Returns busy while the function is configured.
	ResultStatus SetProducer(Producer value) {
		System::CriticalSection(true);
		if (IsConfigured()) {
			System::CriticalSection(false);
			return ResultStatus::busy;
		}

		// A delegate is trivially copyable, so the assignment is short and does no heap work inside the section.
		producer = value;
		System::CriticalSection(false);
		return ResultStatus::ok;
	}


	// Sets the consumer. Returns busy while the function is configured.
	ResultStatus SetConsumer(Consumer value) {
		System::CriticalSection(true);
		if (IsConfigured()) {
			System::CriticalSection(false);
			return ResultStatus::busy;
		}

		consumer = value;
		System::CriticalSection(false);
		return ResultStatus::ok;
	}


protected:
	size_t ProduceIn(std::span<uint8> packets) override {
		if (!producer) {
			return 0;
		}

		size_t written = producer(packets);
		if (written > packets.size()) {
			written = packets.size();
		}

		return written;
	}


	// The producer has already moved past the bytes it handed out, so there is nothing to commit.
	void CommitIn(size_t) override {
	}


	// The producer has already moved past the bytes it handed out, so there is nothing to give back.
	void RollbackIn() override {
	}


	// The producer's next bytes are not known here. Nothing reads the answer, because EndsIdleRunsWithZeroLengthPacket() is false.
	bool HasMoreIn() const override {
		return false;
	}


	// A stream never closes a run with a zero-length packet: its host reads the length it asked for (see the producer's comment).
	bool EndsIdleRunsWithZeroLengthPacket() const override {
		return false;
	}


	bool CanAcceptOut() const override {
		return static_cast<bool>(consumer);
	}


	void StoreOut(std::span<const uint8> data) override {
		if (consumer) {
			consumer(data);
		}
	}


	void ResetData() override {
	}
};
