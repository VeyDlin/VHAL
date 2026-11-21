#pragma once
#include <Drivers/Interface/USB/Class/Common/BulkEndpointEngine.h>
#include <Utilities/Buffer/RingBuffer.h>
#include <algorithm>
#include <array>
#include <span>


// The data behind a bulk engine in buffered mode: an ISR-safe TX ring the application writes, an ISR-safe RX ring the application
// reads, and the transfer buffers of the engine. The owner forwards the engine's hooks to the ProduceIn() ... ResetData() methods
// below (HasMoreIn() goes to HasQueuedIn()), and its public Write(), Read() and the counts to the ring methods (after its own
// session check). Each owner answers EndsIdleRunsWithZeroLengthPacket() with true: the host of a ring channel reads until a short packet.
//
// TxBufferSize and RxBufferSize are the ring sizes in bytes. RxBufferSize must hold one OUT packet, because an OUT transfer is
// armed only when a whole packet fits. PacketsPerTransfer is the length of each IN transfer buffer in packets. IN has InStageCount
// such buffers, so the RAM for IN is InStageCount * PacketsPerTransfer * 64 bytes.
//
// OUT is one packet per transfer, whatever PacketsPerTransfer is. An OUT transfer completes on a short packet or when its buffer is
// full, so a buffer of several packets holds a host write that is a whole number of packets and has no zero-length packet after
// it: the bytes stay in the buffer and never reach Read(). Windows' usbser.sys and WinUSB send no zero-length packet by default, so
// a one-packet buffer is the one that delivers every write whole (measured: a 64-byte write hung on the four-packet OUT buffer of
// the vendor raw channel, and its looped counter stayed at 64).
//
// Each ring call is one critical section, and the copy inside it stays one packet long where the caller can split it: the masked
// interrupts stay short. Critical sections nest, so the ring calls may also run inside the application's own section.
template<uint16 TxBufferSize, uint16 RxBufferSize, uint8 PacketsPerTransfer = 8>
class BufferedBulkChannel {
	static_assert(PacketsPerTransfer > 0, "A transfer buffer holds at least one packet");
	static_assert(RxBufferSize >= BulkEndpointEngine::PacketSize, "The RX ring must hold one whole OUT packet");


public:
	static constexpr size_t TransferSize = static_cast<size_t>(PacketsPerTransfer) * BulkEndpointEngine::PacketSize;


private:
	RingBuffer<uint8, TxBufferSize> txRing;
	RingBuffer<uint8, RxBufferSize> rxRing;
	std::array<std::array<uint8, TransferSize>, BulkEndpointEngine::InStageCount> inBuffers{};
	std::array<uint8, BulkEndpointEngine::PacketSize> outBuffer{};


public:
	// The transfer buffers, for BulkEndpointEngine::SetStages(). The spans point into this object, so it must not move after that.
	std::array<std::span<uint8>, BulkEndpointEngine::InStageCount> GetInStages() {
		std::array<std::span<uint8>, BulkEndpointEngine::InStageCount> stages{};
		for (uint8 index = 0; index < BulkEndpointEngine::InStageCount; index++) {
			stages[index] = inBuffers[index];
		}

		return stages;
	}


	std::span<uint8> GetOutStage() {
		return outBuffer;
	}


	// Queues bytes for the host. Returns how many were queued: fewer than data.size() when the TX ring fills.
	uint16 Write(std::span<const uint8> data) {
		// One ring call per packet: each call is one section, and the copy inside it stays a packet long.
		size_t written = 0;
		while (written < data.size()) {
			size_t chunk = std::min(static_cast<size_t>(BulkEndpointEngine::PacketSize), data.size() - written);
			size_t pushed = txRing.Push(data.subspan(written, chunk));
			written += pushed;
			if (pushed < chunk) {
				break;
			}
		}

		return static_cast<uint16>(written);
	}


	// Copies received bytes into data and returns how many were copied.
	uint16 Read(std::span<uint8> data) {
		// Each chunk is popped in one ring call, so the copy stays a packet long. A reset between two chunks clears the ring, and
		// the next chunk then holds only bytes of the new session, which is what the host sent after the reset.
		size_t total = 0;
		while (total < data.size()) {
			size_t chunk = std::min(static_cast<size_t>(BulkEndpointEngine::PacketSize), data.size() - total);
			size_t popped = rxRing.Pop(data.subspan(total, chunk));
			total += popped;
			if (popped < chunk) {
				break;
			}
		}

		return static_cast<uint16>(total);
	}


	uint16 GetReadableCount() const {
		return rxRing.Size();
	}


	uint16 GetWritableCount() const {
		return txRing.GetFreeSpace();
	}


	// True when bytes wait in the TX ring behind the marked ones, so the next stage would take them at once. While stages are
	// outstanding only Write() adds bytes, and a commit removes marked bytes only, so the answer cannot turn from true to false
	// except by a reset, which the engine answers by not starting a stage at all.
	bool HasQueuedIn() const {
		return txRing.Size() > txRing.GetUncommittedCount();
	}


	// The engine's hooks. The bytes of the stages still outstanding are marked already, so the next stage starts after them. Marked
	// bytes are not removed, and the peek offset skips them.
	size_t ProduceIn(std::span<uint8> packets) {
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
			size_t chunk = std::min(static_cast<size_t>(BulkEndpointEngine::PacketSize), wanted - count);
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
	// all of them come off, this stage's count is committed, and the rest is marked again. The engine calls this from the
	// interrupt only while Pump() holds no claim, and from Pump() while it holds the claim. Either way no ProduceIn() runs at
	// the same time.
	void CommitIn(size_t count) {
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


	void RollbackIn() {
		txRing.RollbackReads();
	}


	// The OUT transfer is one packet, so it is armed when one packet fits in the RX ring.
	bool CanAcceptOut() const {
		return rxRing.GetFreeSpace() >= BulkEndpointEngine::PacketSize;
	}


	// CanAcceptOut() held, so the room for the whole packet is there. The copy goes in packet-sized ring calls, as in Write().
	void StoreOut(std::span<const uint8> data) {
		for (size_t position = 0; position < data.size(); position += BulkEndpointEngine::PacketSize) {
			size_t chunk = std::min(static_cast<size_t>(BulkEndpointEngine::PacketSize), data.size() - position);
			rxRing.Push(data.subspan(position, chunk));
		}
	}


	void ResetData() {
		txRing.Clear();
		rxRing.Clear();
	}
};
