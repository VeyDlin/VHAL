#pragma once
#include <Adapter/USBDeviceAdapter.h>
#include <Drivers/Interface/USB/Device/USBFunction.h>
#include <Drivers/Interface/USB/USBProtocol.h>
#include <System/System.h>
#include <array>
#include <span>


// Shared part of the vendor bulk functions: one bulk IN and one bulk OUT endpoint, 64-byte packets, double-buffered when
// the port allows it. The data path is in the two derived templates: VendorBulkFunction (buffered) and
// VendorBulkStreamFunction (streaming). This class owns the endpoints, the transfer state and the rules both modes share.
//
// Descriptors are not built here. The application composes them with the builders in Descriptor/. For this function it
// writes into its configuration:
//   - one interface descriptor: bInterfaceNumber as passed in the interfaces span (exactly one interface),
//     bInterfaceClass 0xFF (Vendor), bNumEndpoints 2; bInterfaceSubClass and bInterfaceProtocol are the application's;
//   - one bulk IN and one bulk OUT endpoint descriptor, with the two addresses passed in the endpoints span (in either
//     order), wMaxPacketSize 64, bInterval 0. Use 0x81 and 0x02: they are two endpoint numbers, so a port that supports
//     double buffering gives both directions of the pair double buffering. See USBDeviceAdapter::OpenEndpoint() for ports
//     that share one register between the two directions of a number.
//
// IN uses two stages (InStageCount). One stage is in flight on the endpoint while Process() fills the other, so the
// interrupt that completes a transfer only starts a stage that is already filled, and the host's next token finds a
// buffer ready. The stages form a queue: the head is the oldest, the one in flight or the next to start. Each stage is
// one transfer, so the RAM for IN is twice the stage size of the derived template. OUT still uses one stage.
//
// Context: the function keeps no pointer to the device. The core passes the context to each callback, and Process() gets
// it from the application's loop. Callbacks run in interrupt context, or inside the section that Process() opens when a port
// completes a transfer inside the call. They take no section of their own around the context.
// Process() claims the IN side in a short critical section when a stage is free, fills that stage through ProduceIn()
// outside any section, and then leaves the claim in a critical section that also starts the head stage if it is ready.
// The fill stays outside a section so that sections stay short: ProduceIn() copies a whole stage, and interrupts are
// masked for as long as a section is open. While the claim is held, the interrupt
// does not touch the IN queue or the derived class's IN hooks. An interrupt that needs to do so only sets a pending flag,
// and Process() applies it before it leaves the claim. One exception: a bus reset or a configuration clears the derived
// TX and RX rings (ResetData()) from the interrupt even under the claim. Each ring call locks itself, and the application
// calls Write() from the thread that runs Process(), so nothing is added between the clear and the thread's marks. A mark
// made before the clear is rolled back by LeaveClaim(). A mark made after it fails, because RingBuffer::MarkRead() checks
// the size.
class VendorBulkFunctionBase : public USBFunction {
public:
	// Full-speed bulk packet size of both endpoints. A transfer of N packets is N * PacketSize bytes.
	static constexpr uint16 PacketSize = 64;
	// Number of IN stages. The derived template supplies this many transfer buffers.
	static constexpr uint8 InStageCount = 2;
	static_assert(InStageCount == 2, "The IN queue toggles between two stages");


protected:
	// Transfer buffers, supplied by the derived template. The IN stages form a queue, see the comment above the class.
	std::array<std::span<uint8>, InStageCount> inStages;
	std::span<uint8> outStage;


private:
	uint8 inAddress = 0;
	uint8 outAddress = 0;
	bool configured = false;
	// The IN queue. inHead is the oldest stage: the one in flight, or the next one to start. inFilled counts the filled
	// stages from the head on, so the next fill goes to inHead ^ inFilled. Only one side changes the queue at a time: the
	// interrupt while Process() holds no claim, and Process() while it does.
	uint8 inHead = 0;
	uint8 inFilled = 0;
	// Bytes in each IN stage, set when the stage is filled.
	std::array<size_t, InStageCount> inCounts{};
	// Set while the head stage's transfer is in flight. The interrupt clears it when the transfer completes or halts.
	bool inFlight = false;
	// Set by Process() from its claim until it has finished. Only Process() writes it. While it is set, the interrupt does not
	// touch the IN queue or call ProduceIn(), CommitIn() or RollbackIn(). It records what it would have done in the pending
	// flags below.
	bool threadClaimed = false;
	bool inHalted = false;
	// Set by the interrupt while the claim is held. The head's transfer completed: Process() commits its bytes and advances the
	// queue when it leaves the claim.
	bool inDonePending = false;
	// Set by the interrupt while the claim is held. The IN side was halted, or the configuration or session changed: Process()
	// rolls back every stage when it leaves the claim.
	bool inAbandonPending = false;
	bool outArmed = false;
	bool outHalted = false;
	// Counts each configuration and bus reset. Process() compares it before starting a transfer, so bytes staged before a
	// reset or a reconfiguration are never sent after it. It is bumped after the ring is cleared (see Read()).
	uint32 sessionGeneration = 0;


public:
	VendorBulkFunctionBase(std::span<const uint8> interfaces, std::span<const uint8> endpoints)
		: USBFunction(interfaces, endpoints) {
	}


	// The spans point into this object's own transfer buffers, so a copy or a move would leave them pointing into the source.
	VendorBulkFunctionBase(const VendorBulkFunctionBase&) = delete;


	VendorBulkFunctionBase& operator=(const VendorBulkFunctionBase&) = delete;


	VendorBulkFunctionBase(VendorBulkFunctionBase&&) = delete;


	VendorBulkFunctionBase& operator=(VendorBulkFunctionBase&&) = delete;


	ResultStatus OnConfigured(USBDeviceContext& context) override {
		if (GetInterfaces().size() != 1) {
			return ResultStatus::invalidParameter;
		}

		bool hasIn = false;
		bool hasOut = false;
		uint8 newInAddress = 0;
		uint8 newOutAddress = 0;
		for (uint8 address : GetEndpoints()) {
			if (!USBEndpoint::IsValid(address) || USBEndpoint::Number(address) == 0) {
				return ResultStatus::invalidParameter;
			}

			if (USBEndpoint::IsIn(address)) {
				if (hasIn) {
					return ResultStatus::invalidParameter;
				}

				hasIn = true;
				newInAddress = address;
			} else {
				if (hasOut) {
					return ResultStatus::invalidParameter;
				}

				hasOut = true;
				newOutAddress = address;
			}
		}

		if (!hasIn || !hasOut) {
			return ResultStatus::invalidParameter;
		}

		configured = false;
		ResetIn();
		ResultStatus status = OpenPair(context, newInAddress, newOutAddress);
		if (status != ResultStatus::ok) {
			return status;
		}

		inAddress = newInAddress;
		outAddress = newOutAddress;
		inHalted = false;
		outArmed = false;
		outHalted = false;
		ResetData();
		sessionGeneration++;
		configured = true;
		TryArmOut(context);
		return ResultStatus::ok;
	}


	// The ring is not touched here, which keeps the caller's section short. The core calls this inside Stop(), which the caller
	// wraps in a critical section. The stages are forgotten, and the ring keeps its marks until ResetData() on the next
	// configuration drops them. Nothing reads the ring while the function is not configured.
	void OnDeconfigured() override {
		configured = false;
		ForgetIn();
		outArmed = false;
		sessionGeneration++;
	}


	void OnBusReset() override {
		configured = false;
		ResetIn();
		inHalted = false;
		outArmed = false;
		outHalted = false;
		ResetData();
		sessionGeneration++;
	}


	void OnTransferComplete(uint8 address, uint32 byteCount, USBDeviceContext& context) override {
		if (!configured) {
			return;
		}

		if (address == inAddress) {
			if (!inFlight) {
				return;
			}

			inFlight = false;

			// Process() holds the claim, so the ring is its own until it leaves. It commits the stage then.
			if (threadClaimed) {
				inDonePending = true;
				return;
			}

			CommitHead();
			TryStartIn(context);
			return;
		}

		if (address == outAddress) {
			if (!outArmed) {
				return;
			}

			outArmed = false;
			size_t stored = static_cast<size_t>(byteCount);
			if (stored > outStage.size()) {
				stored = outStage.size();
			}

			// The bytes are stored at once, also when the completion runs inside StartOutTransfer() in ProcessOut()'s section. The
			// store opens its own section, which nests in that one, so a reset cannot land between the arm and the store.
			if (stored > 0) {
				StoreOut(outStage.first(stored));
			}

			// A zero-length completion is no data: the endpoint is armed again the same way.
			TryArmOut(context);
		}
	}


	void OnEndpointHaltChanged(uint8 address, bool halted, USBDeviceContext& context) override {
		if (!configured) {
			return;
		}

		if (address == inAddress) {
			inHalted = halted;
			if (!halted) {
				TryStartIn(context);
				return;
			}

			// Stall() abandoned the head's transfer without an event, so every stage's bytes go back to the TX side. While
			// Process() holds the claim, it does the rollback when it leaves.
			if (threadClaimed) {
				inFlight = false;
				inAbandonPending = true;
				return;
			}

			DropIn();
			return;
		}

		if (address == outAddress) {
			outHalted = halted;
			if (halted) {
				// The adapter drops the packets stored so far in this transfer. Nothing has reached the RX side yet.
				outArmed = false;
			} else {
				TryArmOut(context);
			}
		}
	}


	void Process(USBDeviceContext& context) override {
		ProcessIn(context);
		ProcessOut(context);
	}


protected:
	bool IsConfigured() const {
		return configured;
	}


	// Changes at every configuration and bus reset. A derived class reads it before it peeks its rings and checks it after
	// it marks them, so the bytes of an old session are not returned to the application.
	uint32 GetSessionGeneration() const {
		return sessionGeneration;
	}


	// Fills packets with the next bytes to send and returns how many, at most packets.size(). Zero means nothing to send.
	// A buffered derived class also marks these bytes as read, so they stay in its ring until CommitIn() or RollbackIn().
	// Stages can be outstanding together, so the bytes of an earlier stage that is still marked come before these.
	virtual size_t ProduceIn(std::span<uint8> packets) = 0;


	// The head stage's transfer completed, so its first count bytes were sent. Exactly those are committed. The bytes of a stage
	// queued behind it are still outstanding and stay as they are.
	virtual void CommitIn(size_t count) = 0;


	// Every outstanding IN stage was abandoned (a halt, or a start that failed). The bytes ProduceIn() handed out stay for the
	// next start, in the order they were produced.
	virtual void RollbackIn() = 0;


	// True when an OUT transfer may be armed now. While it is false the endpoint NAKs, which keeps the host's data.
	virtual bool CanAcceptOut() const = 0;


	// Takes the bytes of one completed OUT transfer. Called only with at least one byte, and only when CanAcceptOut() held
	// for the transfer, so the derived class has room for all of it.
	virtual void StoreOut(std::span<const uint8> data) = 0;


	// Drops the data the derived class holds, on bus reset and on configuration.
	virtual void ResetData() = 0;


private:
	ResultStatus OpenBulk(USBDeviceContext& context, uint8 address, bool doubleBuffer) {
		USBDeviceAdapter::EndpointConfig config;
		config.address = address;
		config.type = USBEndpointType::Bulk;
		config.maxPacketSize = PacketSize;
		config.doubleBuffer = doubleBuffer;
		return context.OpenEndpoint(config);
	}


	// Reopens an endpoint with double buffering. notSupported means the port cannot give it here, for example when the other
	// direction of the number is open on a port that shares its register (see OpenEndpoint()). The endpoint then stays
	// single-buffered, which is accepted.
	ResultStatus UpgradeBulk(USBDeviceContext& context, uint8 address) {
		ResultStatus status = OpenBulk(context, address, true);
		return status == ResultStatus::notSupported ? ResultStatus::ok : status;
	}


	// Opens both endpoints single-buffered first, then upgrades each one. The order does not depend on the port: IN and OUT on
	// one number are valid, and a port that shares the number's register refuses a double-buffered request while the other
	// direction is open. No transfer is in flight during configuration, so reopening an endpoint is safe. A failure closes the
	// endpoints this call opened, so a failed configuration leaves nothing open.
	ResultStatus OpenPair(USBDeviceContext& context, uint8 newInAddress, uint8 newOutAddress) {
		ResultStatus status = OpenBulk(context, newInAddress, false);
		if (status != ResultStatus::ok) {
			return status;
		}

		status = OpenBulk(context, newOutAddress, false);
		if (status != ResultStatus::ok) {
			context.CloseEndpoint(newInAddress);
			return status;
		}

		status = UpgradeBulk(context, newInAddress);
		if (status == ResultStatus::ok) {
			status = UpgradeBulk(context, newOutAddress);
		}

		if (status != ResultStatus::ok) {
			context.CloseEndpoint(newInAddress);
			context.CloseEndpoint(newOutAddress);
		}

		return status;
	}


	// A transfer shorter than the stage that ends on a packet boundary needs a zero-length packet, or the host keeps reading.
	// A full stage needs none: the host's read completes when the stage is full.
	bool NeedsZeroLengthPacket(size_t count) const {
		return count < inStages[inHead].size() && count % PacketSize == 0;
	}


	// Starts the head stage. The caller sets inFlight before the call, because the interrupt may run the completion at once.
	// DropIn() clears it when the start fails.
	ResultStatus StartHead(USBDeviceContext& context) {
		size_t count = inCounts[inHead];
		return context.StartInTransfer(inAddress, inStages[inHead].first(count), NeedsZeroLengthPacket(count));
	}


	// The head's transfer completed and the derived class takes its bytes. The head leaves the queue, and the stage behind it
	// moves up. Called only from the interrupt while the claim is not held. Process() commits its own completion in LeaveClaim().
	void CommitHead() {
		CommitIn(inCounts[inHead]);
		inHead = static_cast<uint8>(inHead ^ 1);
		inFilled--;
	}


	// Abandons every IN stage: the bytes go back to the derived class, and nothing is in flight. Called only while the claim is
	// not held, or by Process() while it holds it. Not called from a section of the function's own, because RollbackIn() takes
	// the ring's section and the function's sections are kept short.
	void DropIn() {
		RollbackIn();
		ForgetIn();
	}


	// Clears the queue state without touching the ring.
	void ForgetIn() {
		inHead = 0;
		inFilled = 0;
		inFlight = false;
		inDonePending = false;
		inAbandonPending = false;
	}


	// Interrupt side: a configuration or a bus reset starts a new session. The stages go back to the derived class first, so
	// it gets its bytes back before ResetData() clears them. While the claim is held, the rollback is left to Process(), whose
	// LeaveClaim() sees the new session, calls RollbackIn() once and then releases the claim. ForgetIn() clears the flags, so
	// no stale commit follows.
	void ResetIn() {
		if (!threadClaimed) {
			RollbackIn();
		}

		ForgetIn();
	}


	// Interrupt side: starts the head stage when it is filled and idle. When no stage is filled, it produces the head itself,
	// as the single-stage base always did, so a function that calls Process() only once still keeps sending. It does nothing while
	// Process() holds the claim; Process() starts the head when it leaves the claim.
	void TryStartIn(USBDeviceContext& context) {
		if (!configured || threadClaimed || inFlight || inHalted) {
			return;
		}

		if (inFilled == 0) {
			size_t count = ProduceIn(inStages[inHead]);
			if (count == 0) {
				return;
			}

			inCounts[inHead] = count;
			inFilled = 1;
		}

		inFlight = true;
		if (StartHead(context) != ResultStatus::ok) {
			DropIn();
		}
	}


	// Interrupt side: arms the OUT endpoint when the derived class has room for a whole transfer.
	void TryArmOut(USBDeviceContext& context) {
		if (!configured || outArmed || outHalted || !CanAcceptOut()) {
			return;
		}

		outArmed = true;
		if (context.StartOutTransfer(outAddress, outStage) != ResultStatus::ok) {
			outArmed = false;
		}
	}


	// Thread side. The claim is taken under a critical section when a stage is free, and it records the session generation. The
	// stage is filled outside any section, so the section stays short: ProduceIn() copies a whole stage. LeaveClaim() ends the
	// claim: it applies what the interrupt left pending and starts the head stage in the same section that releases the claim.
	void ProcessIn(USBDeviceContext& context) {
		System::CriticalSection(true);
		bool claimed = configured && !inHalted && inFilled < InStageCount && (inFilled == 0 || inFlight);
		uint8 fillIndex = static_cast<uint8>(inHead ^ inFilled);
		uint32 claimedGeneration = sessionGeneration;
		if (claimed) {
			threadClaimed = true;
		}

		System::CriticalSection(false);
		if (!claimed) {
			return;
		}

		size_t count = ProduceIn(inStages[fillIndex]);
		LeaveClaim(context, fillIndex, count, claimedGeneration);
	}


	// Thread side, with the claim held. The interrupt has only set its pending flags, so the ring calls here (CommitIn,
	// RollbackIn) do not overlap an interrupt, apart from the ring clear of a reset (see the class comment). Each round reads
	// the flags and, when nothing is pending, releases the claim in the same critical section that records the fill and
	// starts the head. A failed start rolls back and goes round again, so the release is always checked against the latest
	// flags. The fill counts only when the session and the halt state still match the claim.
	void LeaveClaim(USBDeviceContext& context, uint8 fillIndex, size_t count, uint32 claimedGeneration) {
		bool fillPending = count > 0;
		bool sessionRolledBack = false;

		for (;;) {
			System::CriticalSection(true);
			bool done = inDonePending;
			bool abandon = inAbandonPending;
			bool sameSession = configured && sessionGeneration == claimedGeneration;
			// Read with the head index, so the count belongs to the head even if a reset clears the queue during the commit below.
			size_t headCount = inCounts[inHead];
			inDonePending = false;
			inAbandonPending = false;

			// The session ended under the claim. ForgetIn() has cleared the queue, so only the fill's marks are left to return. That
			// is done once, outside the section, and the claim is then checked again before it is released.
			if (!done && !abandon && !sameSession && !sessionRolledBack) {
				System::CriticalSection(false);
				RollbackIn();
				sessionRolledBack = true;
				fillPending = false;
				continue;
			}

			bool drop = abandon;
			if (!done && !abandon) {
				if (sameSession && fillPending) {
					inCounts[fillIndex] = count;
					inFilled++;
				}

				fillPending = false;
				bool attempt = sameSession && !inHalted && !inFlight && inFilled > 0;
				if (!attempt) {
					threadClaimed = false;
					System::CriticalSection(false);
					return;
				}

				inFlight = true;
				if (StartHead(context) == ResultStatus::ok) {
					// A port may complete the transfer inside the call. The interrupt then set a pending flag, so the flags are read
					// again before the claim is released, and the finished stage is committed rather than left in the queue.
					if (!inDonePending && !inAbandonPending) {
						threadClaimed = false;
						System::CriticalSection(false);
						return;
					}

					System::CriticalSection(false);
					continue;
				}

				// The start failed while the claim is still held. The stages are rolled back below, then the claim is checked again.
				drop = true;
			}

			System::CriticalSection(false);

			// A completed transfer's bytes were sent, so they are committed before anything is rolled back. The ring commit runs
			// outside the section, as the claim allows. The queue advances only in a section of its own, and only when the session
			// is still the claim's: an interrupt that reset the session during the commit has already cleared the queue.
			if (done) {
				CommitIn(headCount);
				System::CriticalSection(true);
				if (configured && sessionGeneration == claimedGeneration) {
					inHead = static_cast<uint8>(inHead ^ 1);
					inFilled--;
				}

				System::CriticalSection(false);
			}

			if (drop) {
				fillPending = false;
				DropIn();
			}
		}
	}


	// Thread side: re-arms the OUT endpoint once the derived class has room again (the application has read). The arm runs
	// in one critical section with the store that a packet delivered inside the call makes (see OnTransferComplete()), so a
	// reset cannot land between the arm and the store. The section nests the ring's own section, which the store opens.
	void ProcessOut(USBDeviceContext& context) {
		if (!configured || outArmed || outHalted || !CanAcceptOut()) {
			return;
		}

		System::CriticalSection(true);
		TryArmOut(context);
		System::CriticalSection(false);
	}
};
