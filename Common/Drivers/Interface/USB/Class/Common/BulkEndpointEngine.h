#pragma once
#include <Adapter/USBDeviceAdapter.h>
#include <Drivers/Interface/USB/Device/USBFunction.h>
#include <Drivers/Interface/USB/USBProtocol.h>
#include <System/System.h>
#include <array>
#include <span>


// The bulk transfer engine: one bulk IN and one bulk OUT endpoint, 64-byte packets, double-buffered when the port allows it. It
// is the part of a function that moves a byte stream, and it does not know which interface or descriptor the endpoints belong
// to. The vendor bulk functions and the CDC-ACM data interface both use it. The owner gives it the two addresses at each session
// (BeginSession()), the two IN and OUT transfer buffers at construction (SetStages()), and implements the eight hooks that move the
// bytes.
// A function forwards its USBFunction callbacks to the engine's Handle*, Pump(), BeginSession() and EndSession() methods.
//
// IN uses two stages (InStageCount). One stage is in flight on the endpoint while Pump() fills the other, so the interrupt that
// completes a transfer only starts a stage that is already filled, and the host's next token finds a buffer ready. The stages
// form a queue: the head is the oldest, the one in flight or the next to start. Each stage is one transfer, so the RAM for IN is
// twice the stage size. OUT still uses one stage.
//
// Context: the engine keeps no pointer to the device. The core passes the context to each callback, and Pump() gets it from the
// application's loop. Callbacks run in interrupt context, or inside the section that Pump() opens when a port completes a
// transfer inside the call. They take no section of their own around the context.
// Pump() claims the IN side in a short critical section when a stage is free, fills that stage through ProduceIn() outside any
// section, and then leaves the claim in a critical section that also starts the head stage if it is ready. The fill stays outside
// a section so that sections stay short: ProduceIn() copies a whole stage, and interrupts are masked for as long as a section is
// open. While the claim is held, the interrupt does not touch the IN queue or the hooks. An interrupt that needs to do so only sets
// a pending flag, and Pump() applies it before it leaves the claim. One exception: a bus reset or a session change clears the
// hooks' data (ResetData()) from the interrupt even under the claim. Each ring call locks itself, and the application calls Write()
// from the thread that runs Pump(), so nothing is added between the clear and the thread's marks. A mark made before the clear is
// rolled back by LeaveClaim(). A mark made after it fails, because RingBuffer::MarkRead() checks the size.
class BulkEndpointEngine {
public:
	// Full-speed bulk packet size of both endpoints. A transfer of N packets is N * PacketSize bytes.
	static constexpr uint16 PacketSize = 64;
	// Number of IN stages. The owner supplies this many transfer buffers.
	static constexpr uint8 InStageCount = 2;
	static_assert(InStageCount == 2, "The IN queue toggles between two stages");


protected:
	// Transfer buffers, set by SetStages(). The owner keeps them alive for the life of the engine. A derived class reads their sizes
	// (the stream bench checks the OUT size), so they are protected.
	std::array<std::span<uint8>, InStageCount> inStages;
	std::span<uint8> outStage;


private:
	uint8 inAddress = 0;
	uint8 outAddress = 0;
	bool configured = false;
	// The IN queue. inHead is the oldest stage: the one in flight, or the next one to start. inFilled counts the filled
	// stages from the head on, so the next fill goes to inHead ^ inFilled. Only one side changes the queue at a time: the
	// interrupt while Pump() holds no claim, and Pump() while it does.
	uint8 inHead = 0;
	uint8 inFilled = 0;
	// Bytes in each IN stage, set when the stage is filled.
	std::array<size_t, InStageCount> inCounts{};
	// Set while the head stage's transfer is in flight. The interrupt clears it when the transfer completes or halts.
	bool inFlight = false;
	// Set by Pump() from its claim until it has finished. Only Pump() writes it. While it is set, the interrupt does not
	// touch the IN queue or call the IN hooks. It records what it would have done in the pending flags below.
	bool threadClaimed = false;
	bool inHalted = false;
	// Set by the interrupt while the claim is held. The head's transfer completed: Pump() commits its bytes and advances the
	// queue when it leaves the claim.
	bool inDonePending = false;
	// Set by the interrupt while the claim is held. The IN side was halted, or the session changed: Pump() rolls back every
	// stage when it leaves the claim.
	bool inAbandonPending = false;
	bool outArmed = false;
	bool outHalted = false;
	// Counts each session start and end. Pump() compares it before starting a transfer, so bytes staged before a reset or a
	// reconfiguration are never sent after it. It is bumped after the data is cleared (see ResetData()).
	uint32 sessionGeneration = 0;


public:
	BulkEndpointEngine() = default;


	// The stages hold pointers into the owner, so a copy or a move would leave them pointing into the source.
	BulkEndpointEngine(const BulkEndpointEngine&) = delete;


	BulkEndpointEngine& operator=(const BulkEndpointEngine&) = delete;


	BulkEndpointEngine(BulkEndpointEngine&&) = delete;


	BulkEndpointEngine& operator=(BulkEndpointEngine&&) = delete;


	virtual ~BulkEndpointEngine() = default;


	// Opens the pair and starts a session: the endpoints are opened (IN and OUT single-buffered, then upgraded), the transfer state
	// is cleared, the data is reset and OUT is armed. The caller has checked its own rules (interface count, endpoint layout). The
	// addresses must be a bulk IN and a bulk OUT with a number other than 0. Any failure leaves no endpoint of this pair open.
	ResultStatus BeginSession(USBDeviceContext& context, uint8 newInAddress, uint8 newOutAddress) {
		if (!USBEndpoint::IsValid(newInAddress) || !USBEndpoint::IsIn(newInAddress) || USBEndpoint::Number(newInAddress) == 0) {
			return ResultStatus::invalidParameter;
		}

		if (!USBEndpoint::IsValid(newOutAddress) || USBEndpoint::IsIn(newOutAddress) || USBEndpoint::Number(newOutAddress) == 0) {
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


	// The session ends (SET_CONFIGURATION(0) or Stop()). The stages are forgotten, but the data is kept until the next BeginSession()
	// clears it: nothing reads it while the engine is not configured. The owner's endpoints are closed by the core after this.
	void EndSession() {
		configured = false;
		// Under the claim the queue is left to LeaveClaim(), as in ResetIn(): a completion the interrupt already reported is committed.
		if (!threadClaimed) {
			ForgetIn();
		}

		outArmed = false;
		sessionGeneration++;
	}


	// Bus reset: the session ends and the data is cleared. The core has already closed the endpoints.
	void ResetSession() {
		configured = false;
		ResetIn();
		inHalted = false;
		outArmed = false;
		outHalted = false;
		ResetData();
		sessionGeneration++;
	}


	// A transfer on one of the pair's endpoints completed. Other addresses are ignored, so the owner can forward every completion.
	void HandleTransferComplete(uint8 address, uint32 byteCount, USBDeviceContext& context) {
		if (!configured) {
			return;
		}

		if (address == inAddress) {
			if (!inFlight) {
				return;
			}

			inFlight = false;

			// Pump() holds the claim, so the ring is its own until it leaves. It commits the stage then.
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


	// A halt or a clear of one of the pair's endpoints. Other addresses are ignored.
	void HandleEndpointHalt(uint8 address, bool halted, USBDeviceContext& context) {
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
			// Pump() holds the claim, it does the rollback when it leaves.
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


	// Thread side: moves the bytes in both directions. Call it from the owner's Process().
	void Pump(USBDeviceContext& context) {
		ProcessIn(context);
		ProcessOut(context);
	}


	// True while a session is open. A function's Write() and Read() return 0 without it.
	bool IsConfigured() const {
		return configured;
	}


	// Changes at every session start and end. A caller reads it before it peeks its data and checks it after it marks the data, so
	// the bytes of an old session are not returned to the application.
	uint32 GetSessionGeneration() const {
		return sessionGeneration;
	}


protected:
	// Gives the engine its transfer buffers. Called once by the owner, before the device starts.
	void SetStages(const std::array<std::span<uint8>, InStageCount>& inBuffers, std::span<uint8> outBuffer) {
		inStages = inBuffers;
		outStage = outBuffer;
	}


	// Fills packets with the next bytes to send and returns how many, at most packets.size(). Zero means nothing to send.
	// A buffered owner also marks these bytes as read, so they stay in its data until CommitIn() or RollbackIn().
	// Stages can be outstanding together, so the bytes of an earlier stage that is still marked come before these.
	virtual size_t ProduceIn(std::span<uint8> packets) = 0;


	// The head stage's transfer completed, so its first count bytes were sent. Exactly those are committed. The bytes of a stage
	// queued behind it are still outstanding and stay as they are.
	virtual void CommitIn(size_t count) = 0;


	// Every outstanding IN stage was abandoned (a halt, or a start that failed). The bytes ProduceIn() handed out stay for the
	// next start, in the order they were produced.
	virtual void RollbackIn() = 0;


	// True when the owner holds bytes behind the outstanding stages, so the next stage is produced at once when the head completes.
	// NeedsZeroLengthPacket() reads it for a whole-packet stage, and only when EndsIdleRunsWithZeroLengthPacket() holds. An owner
	// that cannot tell answers false: an idle run then ends with a zero-length packet, which costs one packet and never holds the host.
	virtual bool HasMoreIn() const = 0;


	// True when a whole-packet run of IN data that ends with nothing queued behind it is closed with a zero-length packet, so the
	// host's read ends there. A buffered owner answers true: its host (usbser.sys, WinUSB) reads until a short packet. An owner whose
	// host reads the length it asked for answers false, and then no run ends with a zero-length packet: the host's transfer waits
	// for the next data instead. A count that is not a whole number of packets always ends with its short packet, whatever this says.
	virtual bool EndsIdleRunsWithZeroLengthPacket() const = 0;


	// True when an OUT transfer may be armed now. While it is false the endpoint NAKs, which keeps the host's data.
	virtual bool CanAcceptOut() const = 0;


	// Takes the bytes of one completed OUT transfer. Called only with at least one byte, and only when CanAcceptOut() held
	// for the transfer, so the owner has room for all of it.
	virtual void StoreOut(std::span<const uint8> data) = 0;


	// Drops the data the owner holds, on bus reset and on a new session.
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


	// The end of a run of IN data. A count that is not a whole number of packets ends with a short packet. A whole number of packets
	// ends with a zero-length packet only when the owner closes idle runs with one (EndsIdleRunsWithZeroLengthPacket()), and nothing
	// waits behind it: the stage behind is not filled and the owner holds no bytes. Then the next stage does not follow at once, and
	// the host's read has to end here. Otherwise the next stage starts when this one completes and no packet is added. The rule is
	// the same for a stage of any length, a short whole-packet stage and a full one alike. A stage's packet count is fixed when it
	// starts, so a stage that starts with nothing behind it keeps its zero-length packet even if bytes arrive later.
	// The answer may be stale in one direction only: bytes that arrive after the check make the zero-length packet unneeded, and
	// are sent in the next transfer anyway. A zero-length packet sent too early costs one packet; a missing one holds the host.
	// A stage starts only when no other stage is filled behind it, so HasMoreIn() is the only thing that can be queued behind it.
	bool NeedsZeroLengthPacket(size_t count) const {
		if (count % PacketSize != 0 || !EndsIdleRunsWithZeroLengthPacket()) {
			return false;
		}

		return !HasMoreIn();
	}


	// Starts the head stage. The caller sets inFlight before the call, because the interrupt may run the completion at once.
	// DropIn() clears it when the start fails.
	ResultStatus StartHead(USBDeviceContext& context) {
		size_t count = inCounts[inHead];
		return context.StartInTransfer(inAddress, inStages[inHead].first(count), NeedsZeroLengthPacket(count));
	}


	// The head's transfer completed and the owner takes its bytes. The head leaves the queue, and the stage behind it moves up.
	// Called only from the interrupt while the claim is not held. Pump() commits its own completion in LeaveClaim().
	void CommitHead() {
		CommitIn(inCounts[inHead]);
		inHead = static_cast<uint8>(inHead ^ 1);
		inFilled--;
	}


	// Abandons every IN stage: the bytes go back to the owner, and nothing is in flight. Called only while the claim is not
	// held, or by Pump() while it holds it. Not called from a section of the engine's own, because RollbackIn() takes the
	// ring's section and the engine's sections are kept short.
	void DropIn() {
		RollbackIn();
		ForgetIn();
	}


	// Clears the queue state without touching the owner's data.
	void ForgetIn() {
		inHead = 0;
		inFilled = 0;
		inFlight = false;
		inDonePending = false;
		inAbandonPending = false;
	}


	// Interrupt side: a session start or a bus reset starts a new session. Without the claim, the stages go back to the owner
	// first, so it gets its bytes back before ResetData() clears them, and the queue is forgotten. With the claim held, this only
	// changes the session generation. LeaveClaim() then sees the new session: it commits a stage that completed before the reset,
	// because its bytes went out, forgets the queue and rolls back the rest. The queue and the pending flags are left alone under
	// the claim, so a completion the interrupt already reported is not lost.
	void ResetIn() {
		if (threadClaimed) {
			sessionGeneration++;
			return;
		}

		RollbackIn();
		ForgetIn();
	}


	// Interrupt side: starts the head stage when it is filled and idle. When no stage is filled, it produces the head itself, so
	// a function that calls Pump() only once still keeps sending. It does nothing while Pump() holds the claim; Pump() starts the
	// head when it leaves the claim.
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


	// Interrupt side: arms the OUT endpoint when the owner has room for a whole transfer.
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


	// Thread side, with the claim held. The interrupt has only set its pending flags, so the hook calls here (CommitIn, RollbackIn)
	// do not overlap an interrupt, apart from the data clear of a reset (see the class comment). Each round reads the flags and,
	// when nothing is pending, releases the claim in the same critical section that records the fill and starts the head. A failed
	// start rolls back and goes round again, so the release is always checked against the latest flags. The fill counts only when
	// the session and the halt state still match the claim.
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

			// The session ended under the claim. The queue is forgotten in this section, so only the fill's marks are left to return.
			// That is done once, outside the section, and the claim is then checked again before it is released.
			if (!done && !abandon && !sameSession && !sessionRolledBack) {
				ForgetIn();
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

			// A completed transfer's bytes were sent, so they are committed before anything is rolled back. The owner's commit runs
			// outside the section, as the claim allows. The queue advances only in a section of its own, and only when the session
			// is still the claim's: an interrupt that reset the session during the commit has already cleared the queue.
			// When the session changed before the commit, the interrupt has cleared the owner's data already, so CommitIn() gets a count
			// for bytes that are gone. The owner must reject it: a buffered owner's RingBuffer::MarkRead() checks the size and refuses.
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


	// Thread side: re-arms the OUT endpoint once the owner has room again (the application has read). The arm runs in one
	// critical section with the store that a packet delivered inside the call makes (see HandleTransferComplete()), so a reset
	// cannot land between the arm and the store. The section nests the ring's own section, which the store opens.
	void ProcessOut(USBDeviceContext& context) {
		if (!configured || outArmed || outHalted || !CanAcceptOut()) {
			return;
		}

		System::CriticalSection(true);
		TryArmOut(context);
		System::CriticalSection(false);
	}
};
