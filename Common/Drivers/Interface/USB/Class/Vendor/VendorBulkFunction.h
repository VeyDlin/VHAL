#pragma once
#include <Drivers/Interface/USB/Class/Vendor/VendorBulkFunctionBase.h>
#include <Utilities/Buffer/RingBuffer.h>
#include <algorithm>
#include <array>
#include <span>


// Buffered mode of the vendor bulk function. The application queues bytes with Write() and takes received bytes with
// Read(). Both rings are ISR-safe: a RingBuffer enters a critical section for each call. Critical sections nest, so Write()
// and Read() may also be called inside the application's own section. That section masks the interrupts for its whole
// length, so it should stay short.
//
// Write() does not start a transfer. Process() fills the free IN stage and starts a transfer when the endpoint is idle, and
// every IN completion starts the next filled stage, so the application must call Process() from its loop (or a thread) to
// send data that is not followed by a completion. Process() also fills the second IN stage while the first one is in
// flight, so the interrupt does not have to read the TX ring at a transfer's end.
// Read() frees room, and Process() arms the OUT endpoint again once a whole transfer fits. While it does not fit, the
// endpoint NAKs and the host keeps its data.
//
// TxBufferSize and RxBufferSize are the ring sizes in bytes. RxBufferSize must hold one whole OUT transfer, because a
// transfer is armed only when all of it fits. PacketsPerTransfer is the length of each transfer buffer in packets. IN has
// InStageCount such buffers, so the RAM for IN is InStageCount * PacketsPerTransfer * 64 bytes.
template<uint16 TxBufferSize, uint16 RxBufferSize, uint8 PacketsPerTransfer = 8>
class VendorBulkFunction : public VendorBulkFunctionBase {
	static_assert(PacketsPerTransfer > 0, "A transfer buffer holds at least one packet");
	static_assert(RxBufferSize >= PacketsPerTransfer * PacketSize, "The RX ring must hold one whole OUT transfer");


public:
	static constexpr size_t TransferSize = static_cast<size_t>(PacketsPerTransfer) * PacketSize;


private:
	RingBuffer<uint8, TxBufferSize> txRing;
	RingBuffer<uint8, RxBufferSize> rxRing;
	std::array<std::array<uint8, TransferSize>, InStageCount> inBuffers{};
	std::array<uint8, TransferSize> outBuffer{};


public:
	VendorBulkFunction(std::span<const uint8> interfaces, std::span<const uint8> endpoints)
		: VendorBulkFunctionBase(interfaces, endpoints) {
		for (uint8 index = 0; index < InStageCount; index++) {
			inStages[index] = inBuffers[index];
		}

		outStage = outBuffer;
	}


	// From the fluent builder's layout (USBDescriptors::Function()).
	explicit VendorBulkFunction(const USBFunctionLayout& layout)
		: VendorBulkFunction(layout.interfaces, layout.endpoints) {
	}


	// Queues bytes for the host. Returns how many were queued: fewer than data.size() when the TX ring fills, and 0 while
	// the function is not configured.
	uint16 Write(std::span<const uint8> data) {
		if (!IsConfigured()) {
			return 0;
		}

		// One ring call per packet: each call is one section, and the copy inside it stays a packet long.
		size_t written = 0;
		while (written < data.size()) {
			size_t chunk = std::min(static_cast<size_t>(PacketSize), data.size() - written);
			size_t pushed = txRing.Push(data.subspan(written, chunk));
			written += pushed;
			if (pushed < chunk) {
				break;
			}
		}

		return static_cast<uint16>(written);
	}


	// Copies received bytes into data and returns how many were copied. Returns 0 while the function is not configured.
	uint16 Read(std::span<uint8> data) {
		if (!IsConfigured()) {
			return 0;
		}

		// Each chunk is popped in one ring call, so the copy stays a packet long. A reset between two chunks clears the ring, and
		// the next chunk then holds only bytes of the new session, which is what the host sent after the reset.
		size_t total = 0;
		while (total < data.size()) {
			size_t chunk = std::min(static_cast<size_t>(PacketSize), data.size() - total);
			size_t popped = rxRing.Pop(data.subspan(total, chunk));
			total += popped;
			if (popped < chunk) {
				break;
			}
		}

		return static_cast<uint16>(total);
	}


	// Received bytes waiting for Read().
	uint16 GetReadableCount() const {
		if (!IsConfigured()) {
			return 0;
		}

		return rxRing.Size();
	}


	// Bytes Write() can queue now.
	uint16 GetWritableCount() const {
		if (!IsConfigured()) {
			return 0;
		}

		return txRing.GetFreeSpace();
	}


protected:
	// The bytes of the stages still outstanding are marked already, so the next stage starts after them. Marked bytes are not
	// removed, and the peek offset skips them.
	size_t ProduceIn(std::span<uint8> packets) override {
		uint16 outstanding = txRing.GetUncommittedCount();
		uint16 available = txRing.Size() - outstanding;
		size_t wanted = packets.size();
		if (wanted > available) {
			wanted = available;
		}

		// One peek per packet, each at its own offset past the marked bytes, so no ring call copies more than a packet. The
		// marks are made once, after the copy, so the stage is either marked whole or not at all.
		size_t count = 0;
		while (count < wanted) {
			size_t chunk = std::min(static_cast<size_t>(PacketSize), wanted - count);
			uint16 peeked = txRing.PeekMultiple(packets.data() + count, static_cast<uint16>(chunk), static_cast<uint16>(outstanding + count));
			count += peeked;
			if (peeked < chunk) {
				break;
			}
		}

		if (count == 0) {
			return 0;
		}

		// Marked, not removed: the bytes leave the ring at CommitIn(). An abandoned transfer therefore loses nothing.
		if (txRing.MarkRead(static_cast<uint16>(count)) != ResultStatus::ok) {
			return 0;
		}

		return count;
	}


	// Commits the oldest count marked bytes. CommitReads() would commit every mark, including those of a stage queued behind
	// this one that is still in flight or waiting. So when more is marked than this stage holds, the marks are made again:
	// all of them come off, this stage's count is committed, and the rest is marked again. The base calls this from the
	// interrupt only while Process() holds no claim, and from Process() while it holds the claim. Either way no ProduceIn()
	// runs at the same time.
	void CommitIn(size_t count) override {
		uint16 marked = txRing.GetUncommittedCount();
		if (count == marked) {
			txRing.CommitReads();
			return;
		}

		txRing.RollbackReads();
		txRing.MarkRead(static_cast<uint16>(count));
		txRing.CommitReads();
		txRing.MarkRead(static_cast<uint16>(marked - count));
	}


	void RollbackIn() override {
		txRing.RollbackReads();
	}


	bool CanAcceptOut() const override {
		return rxRing.GetFreeSpace() >= outStage.size();
	}


	// CanAcceptOut() held, so the room for the whole transfer is there. The copy goes in packet-sized ring calls, as in Write().
	void StoreOut(std::span<const uint8> data) override {
		for (size_t position = 0; position < data.size(); position += PacketSize) {
			size_t chunk = std::min(static_cast<size_t>(PacketSize), data.size() - position);
			rxRing.Push(data.subspan(position, chunk));
		}
	}


	void ResetData() override {
		txRing.Clear();
		rxRing.Clear();
	}
};
