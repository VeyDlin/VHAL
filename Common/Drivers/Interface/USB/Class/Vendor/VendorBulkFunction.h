#pragma once
#include <Drivers/Interface/USB/Class/Common/BufferedBulkChannel.h>
#include <Drivers/Interface/USB/Class/Vendor/VendorBulkFunctionBase.h>
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
// Read() frees room, and Process() arms the OUT endpoint again once one packet fits. While it does not fit, the endpoint NAKs
// and the host keeps its data. Each OUT transfer is one packet, so a host write of any length reaches Read() whole, with or
// without a zero-length packet after it (see BufferedBulkChannel).
//
// TxBufferSize and RxBufferSize are the ring sizes in bytes (see BufferedBulkChannel). PacketsPerTransfer is the length of each
// IN transfer buffer in packets. OUT does not use it.
template<uint16 TxBufferSize, uint16 RxBufferSize, uint8 PacketsPerTransfer = 8>
class VendorBulkFunction : public VendorBulkFunctionBase {
public:
	// The IN transfer size. An OUT transfer is one packet, whatever the template says (see BufferedBulkChannel).
	static constexpr size_t TransferSize = BufferedBulkChannel<TxBufferSize, RxBufferSize, PacketsPerTransfer>::TransferSize;


private:
	BufferedBulkChannel<TxBufferSize, RxBufferSize, PacketsPerTransfer> channel;


public:
	VendorBulkFunction(std::span<const uint8> interfaces, std::span<const uint8> endpoints)
		: VendorBulkFunctionBase(interfaces, endpoints) {
		SetBulkStages(channel.GetInStages(), channel.GetOutStage());
	}


	// From the fluent builder's layout (USBDescriptors::Function()).
	explicit VendorBulkFunction(const USBFunctionLayout& layout)
		: VendorBulkFunction(layout.interfaces, layout.endpoints) {
	}


	// True while the device is configured. The bootloader's transport reads it.
	using VendorBulkFunctionBase::IsConfigured;


	// The session counter, for a caller that must notice a reset between two of its calls.
	using VendorBulkFunctionBase::GetSessionGeneration;


	// Queues bytes for the host. Returns how many were queued: fewer than data.size() when the TX ring fills, and 0 while
	// the function is not configured.
	uint16 Write(std::span<const uint8> data) {
		if (!IsConfigured()) {
			return 0;
		}

		return channel.Write(data);
	}


	// Copies received bytes into data and returns how many were copied. Returns 0 while the function is not configured.
	uint16 Read(std::span<uint8> data) {
		if (!IsConfigured()) {
			return 0;
		}

		return channel.Read(data);
	}


	// Received bytes waiting for Read().
	uint16 GetReadableCount() const {
		if (!IsConfigured()) {
			return 0;
		}

		return channel.GetReadableCount();
	}


	// Bytes Write() can queue now.
	uint16 GetWritableCount() const {
		if (!IsConfigured()) {
			return 0;
		}

		return channel.GetWritableCount();
	}


protected:
	size_t ProduceIn(std::span<uint8> packets) override {
		return channel.ProduceIn(packets);
	}


	void CommitIn(size_t count) override {
		channel.CommitIn(count);
	}


	void RollbackIn() override {
		channel.RollbackIn();
	}


	bool HasMoreIn() const override {
		return channel.HasQueuedIn();
	}


	// The host of a ring channel reads until a short packet, so an idle run ends with a zero-length packet.
	bool EndsIdleRunsWithZeroLengthPacket() const override {
		return true;
	}


	bool CanAcceptOut() const override {
		return channel.CanAcceptOut();
	}


	void StoreOut(std::span<const uint8> data) override {
		channel.StoreOut(data);
	}


	void ResetData() override {
		channel.ResetData();
	}
};
