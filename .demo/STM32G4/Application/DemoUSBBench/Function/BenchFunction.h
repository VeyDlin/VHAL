#pragma once
#include <BenchProtocol.h>
#include <Drivers/Interface/USB/Class/Vendor/VendorBulkStreamFunction.h>
#include <Drivers/Interface/USB/Device/USBFunction.h>
#include <Drivers/Interface/USB/USBProtocol.h>
#include <Utilities/Buffer/RingBuffer.h>
#include <System/System.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <span>


namespace DemoUSBBench {
	// Packets per IN transfer buffer: 32 packets of 64 bytes, a 2048-byte transfer. Longer transfers spend less time between one
	// transfer's last packet and the next transfer's first, when the endpoints are idle. OUT is one packet per transfer, so this
	// does not apply to it.
	constexpr uint8 BenchPacketsPerTransfer = 32;


	// The benchmark function: the vendor bulk streaming function with the modes and vendor requests of BenchProtocol.h. Its IN
	// stages are filled by the vendor base in Process(), called from the main loop, and by the interrupt only when no stage is
	// ready; OUT data is taken in the interrupt. StoreOut() takes every OUT packet, as a stream's consumer must (see
	// VendorBulkStreamFunction), because the function has no backpressure. The bench sends no zero-length packet after a whole
	// number of packets: its host reads the length it asked for. The main loop reads the counters only through
	// TakeCountersSnapshot(), which copies them under a critical section.
	class BenchFunction : public VendorBulkStreamFunction<BenchPacketsPerTransfer> {
	private:
		// Loopback ring: four IN transfers of 2048 bytes. The OUT side arms only when the ring has room for one packet
		// (CanAcceptOut), so the ring holds several transfers' worth to keep both directions busy at the same time.
		static constexpr uint16 LoopbackSize = 8192;
		static constexpr uint32 NoErrorOffset = 0xFFFFFFFF;

		// What one IN stage needs to be committed or rolled back. Source stages record the generator state they started from; a
		// loopback stage records only its size, its bytes stay marked in the loopback ring.
		struct StagedIn {
			BenchProtocol::PatternGenerator sourceStart;
			uint32 bytes = 0;
			bool fromLoopback = false;
		};

		BenchProtocol::Mode mode = BenchProtocol::Mode::Idle;
		// Generates the bytes sent (Source, SourceAndSink).
		BenchProtocol::PatternGenerator sourceGenerator;
		// Generates the bytes expected from the host (Sink, SourceAndSink).
		BenchProtocol::PatternGenerator sinkGenerator;
		BenchProtocol::Counters counters{};
		// Bytes checked since the last SetMode. firstErrorOffset is counted in this stream, the one the generator checks, so it
		// still points into the pattern after a ResetCounters. 64 bits, so the count does not wrap in practice; firstErrorOffset
		// itself is 32 bits (see the comment in CheckPattern()).
		uint64 sinkOffset = 0;
		// The adapter error total the device reported last. The device counts from its own start, so this is the baseline for
		// the next report.
		uint32 usbErrorTotal = 0;
		// The IN stages the vendor base has taken from ProduceIn() and not yet committed or rolled back, oldest first. The base
		// keeps at most InStageCount of them, so the array is that long. Bytes are counted into bytesSent only when their stage
		// completes, so an abandoned stage does not count as sent.
		std::array<StagedIn, VendorBulkFunctionBase::InStageCount> staged{};
		uint8 stagedCount = 0;
		// Changes whenever the source generator is set from outside a fill (SetMode, rollback). A fill that started before a change
		// is discarded, so a stale fill never overwrites the generator.
		uint32 producerGeneration = 0;
		RingBuffer<uint8, LoopbackSize> loopback;
		// The GetCounters answer, little-endian. It lives in the object because the data stage reads it after the call returns.
		std::array<uint8, BenchProtocol::CountersSize> answer{};
		bool reconnectRequested = false;
		uint16 reconnectDelayMs = 0;


	public:
		explicit BenchFunction(const USBFunctionLayout& layout)
			: VendorBulkStreamFunction<BenchPacketsPerTransfer>(layout) {
			counters.firstErrorOffset = NoErrorOffset;
		}


		// Reports the device's adapter error total. usbErrors counts the increase since the previous report, and counts from zero
		// after a ResetCounters. A total below the previous one means the device counted from zero again, so the whole total is new.
		void SetUsbErrorTotal(uint32 total) {
			System::CriticalSection(true);
			if (total >= usbErrorTotal) {
				counters.usbErrors += total - usbErrorTotal;
			} else {
				counters.usbErrors += total;
			}

			usbErrorTotal = total;
			System::CriticalSection(false);
		}


		// Called after the device restarts (Reconnect): the device counts its adapter errors from zero again, so the baseline is
		// zero too. Called from the main loop, which is the only caller of SetUsbErrorTotal().
		void ResetUsbErrorBaseline() {
			System::CriticalSection(true);
			usbErrorTotal = 0;
			System::CriticalSection(false);
		}


		void TakeCountersSnapshot(BenchProtocol::Counters& out) {
			System::CriticalSection(true);
			out = counters;
			System::CriticalSection(false);
		}


		BenchProtocol::Mode GetMode() const {
			return mode;
		}


		// Returns true once for each Reconnect request, with the time the host asked the pull-up to stay off.
		bool TakeReconnectRequest(uint16& delayMs) {
			System::CriticalSection(true);
			bool pending = reconnectRequested;
			delayMs = reconnectDelayMs;
			reconnectRequested = false;
			System::CriticalSection(false);
			return pending;
		}


		void OnBusReset() override {
			VendorBulkStreamFunction<BenchPacketsPerTransfer>::OnBusReset();
			counters.busResets++;
		}


		ResultStatus OnControlRequest(const USBSetupPacket& setup, USBDeviceContext& context) override {
			if (setup.type != USBRequestType::Vendor || setup.recipient != USBRecipient::Interface) {
				return ResultStatus::notSupported;
			}

			if (setup.index != BenchProtocol::InterfaceNumber) {
				return ResultStatus::notSupported;
			}

			if (setup.request == static_cast<uint8>(BenchProtocol::Request::GetCounters)) {
				return SendCounters(setup, context);
			}

			// The other requests carry no data stage. A request that sends data is refused, which stalls it.
			if (setup.direction != USBDirection::Out || setup.length != 0) {
				return ResultStatus::invalidParameter;
			}

			if (setup.request == static_cast<uint8>(BenchProtocol::Request::SetMode)) {
				if (setup.value > static_cast<uint16>(BenchProtocol::Mode::Loopback)) {
					return ResultStatus::invalidParameter;
				}

				ApplyMode(static_cast<BenchProtocol::Mode>(setup.value));
				return ResultStatus::ok;
			}

			if (setup.request == static_cast<uint8>(BenchProtocol::Request::ResetCounters)) {
				ClearCounters();
				return ResultStatus::ok;
			}

			if (setup.request == static_cast<uint8>(BenchProtocol::Request::Reconnect)) {
				// Acknowledged here: the core sends the status stage when this returns ok. The main loop drops D+ afterwards, so
				// the disconnect never cuts the request short.
				reconnectDelayMs = setup.value;
				reconnectRequested = true;
				return ResultStatus::ok;
			}

			return ResultStatus::notSupported;
		}


	protected:
		size_t ProduceIn(std::span<uint8> packets) override {
			if (mode == BenchProtocol::Mode::Source || mode == BenchProtocol::Mode::SourceAndSink) {
				return FillPattern(packets);
			}

			if (mode == BenchProtocol::Mode::Loopback) {
				return TakeLoopback(packets);
			}

			return 0;
		}


		// The oldest stage completed. The stage behind it, if any, stays staged until its own transfer completes. The engine calls
		// this outside any section when a claimed Pump() commits, so a bus reset can land just before it. ResetData() does not give
		// the stages back, so the completed stage is still staged then, and the commit counts it: its bytes went out before the reset.
		// An empty list means nothing to commit, and the commit then does nothing.
		void CommitIn(size_t count) override {
			bool fromLoopback = false;
			System::CriticalSection(true);
			if (stagedCount > 0) {
				StagedIn completed = PopStaged();
				fromLoopback = completed.fromLoopback;
				counters.bytesSent += count;
			}

			System::CriticalSection(false);

			// The ring commits every marked read at once. Only one loopback stage is ever outstanding (TakeLoopback enforces it),
			// so this commits exactly the completed stage. After a reset the loopback ring is empty and the commit does nothing.
			if (fromLoopback) {
				loopback.CommitReads();
			}
		}


		// Every outstanding stage was abandoned, so the pattern bytes go back to the generator and the loopback bytes are unmarked,
		// so the next start sends them again.
		void RollbackIn() override {
			RestoreStaged();
		}


		bool CanAcceptOut() const override {
			if (mode == BenchProtocol::Mode::Loopback) {
				return loopback.GetFreeSpace() >= outStage.size();
			}

			// Other modes accept and drop the host's data, so the OUT endpoint never NAKs.
			return true;
		}


		void StoreOut(std::span<const uint8> data) override {
			counters.bytesReceived += data.size();
			if (mode == BenchProtocol::Mode::Sink || mode == BenchProtocol::Mode::SourceAndSink) {
				CheckPattern(data);
			}

			// One ring call per packet, so the copy in each section stays a packet long. StoreOut() gets one packet per call, and
			// CanAcceptOut() made room for it, so the loop below takes one pass.
			if (mode == BenchProtocol::Mode::Loopback) {
				for (size_t position = 0; position < data.size(); position += BenchProtocol::BulkPacketSize) {
					size_t chunk = std::min(static_cast<size_t>(BenchProtocol::BulkPacketSize), data.size() - position);
					loopback.Push(data.subspan(position, chunk));
				}
			}
		}


		// Only the loopback ring is cleared here. The stages go back through RollbackIn(), which the engine calls once for each session
		// change: ResetIn() when no claim is held, and LeaveClaim() when one is. Restoring here as well would give a stage back twice when
		// a reset lands inside a claimed Pump(), and it would empty the staged list that the claim's commit still reads.
		void ResetData() override {
			loopback.Clear();
		}


	private:
		// SetMode: a new mode starts both generators from the seed, and the counters start from zero.
		void ApplyMode(BenchProtocol::Mode value) {
			mode = value;
			sourceGenerator.Reset();
			sinkGenerator.Reset();
			// Stages already handed out still go out. If one is abandoned later, its bytes restart from the new seed, as the new mode does.
			for (uint8 index = 0; index < stagedCount; index++) {
				staged[index].sourceStart.Reset();
			}

			producerGeneration++;
			sinkOffset = 0;
			loopback.Clear();
			ClearCounters();
		}


		// ResetCounters: the counters go back to zero, the mode and the generators stay as they are. usbErrors starts again
		// from zero. Adapter errors counted between the last report and this call are counted into the new value as well, so
		// it can include up to one report interval of errors from before the reset.
		void ClearCounters() {
			counters = BenchProtocol::Counters{};
			counters.firstErrorOffset = NoErrorOffset;
		}


		ResultStatus SendCounters(const USBSetupPacket& setup, USBDeviceContext& context) {
			if (setup.direction != USBDirection::In) {
				return ResultStatus::invalidParameter;
			}

			PutLittleEndian(0, counters.bytesSent, 8);
			PutLittleEndian(8, counters.bytesReceived, 8);
			PutLittleEndian(16, counters.receiveErrors, 4);
			PutLittleEndian(20, counters.firstErrorOffset, 4);
			PutLittleEndian(24, counters.busResets, 4);
			PutLittleEndian(28, counters.usbErrors, 4);
			return context.ControlSendData(answer);
		}


		void PutLittleEndian(size_t offset, uint64 value, size_t size) {
			for (size_t index = 0; index < size; index++) {
				answer[offset + index] = static_cast<uint8>(value >> (8 * index));
			}
		}


		// The fill runs with interrupts enabled. It takes tens of microseconds, and a masked section that long would delay the
		// release of the next IN buffer. So it works on a copy, and the copy is committed only when no SetMode or rollback ran
		// during the fill; otherwise the fill is discarded and the base asks again.
		size_t FillPattern(std::span<uint8> packets) {
			System::CriticalSection(true);
			BenchProtocol::PatternGenerator start = sourceGenerator;
			uint32 generation = producerGeneration;
			System::CriticalSection(false);

			BenchProtocol::PatternGenerator next = start;
			next.Fill(packets);

			System::CriticalSection(true);
			bool committed = generation == producerGeneration;
			if (committed) {
				sourceGenerator = next;
				PushStaged(StagedIn{ start, static_cast<uint32>(packets.size()), false });
			}

			System::CriticalSection(false);
			return committed ? packets.size() : 0;
		}


		size_t TakeLoopback(std::span<uint8> packets) {
			// Only one loopback stage is outstanding at a time. A staged loopback record blocks the next take even when the ring has
			// no marks: ApplyMode() clears the marks while the stage is still in flight. PeekMultiple() reads from the ring's start,
			// which holds the marked bytes of the stage in flight, and the ring commits all marks at once, so a second stage could
			// not be committed alone.
			System::CriticalSection(true);
			bool stageOutstanding = HasStagedLoopback();
			System::CriticalSection(false);
			if (stageOutstanding || loopback.GetUncommittedCount() > 0) {
				return 0;
			}

			uint16 wanted = static_cast<uint16>(packets.size());
			if (wanted > loopback.Size()) {
				wanted = loopback.Size();
			}

			// One peek per packet keeps each critical section to a packet, as the vendor function does. The ring's calls each take
			// their own critical section, and sections do not nest, so the peeks and the mark below are separate steps. ApplyMode()
			// can run between them, during a SetMode request. The mark then either fails, because the ring was emptied, or marks
			// bytes that arrived after the clear. In the second case the loopback stream after that SetMode sends or skips up to one
			// stage of bytes. SetMode already restarts the host's stream, so this is accepted rather than locked out.
			uint16 count = 0;
			while (count < wanted) {
				uint16 chunk = static_cast<uint16>(wanted - count);
				if (chunk > BenchProtocol::BulkPacketSize) {
					chunk = BenchProtocol::BulkPacketSize;
				}

				uint16 peeked = loopback.PeekMultiple(packets.data() + count, chunk, count);
				count = static_cast<uint16>(count + peeked);
				if (peeked < chunk) {
					break;
				}
			}

			if (count == 0) {
				return 0;
			}

			// The bytes stay in the ring, marked as read, until CommitIn() or RollbackIn() settles the transfer.
			if (loopback.MarkRead(count) != ResultStatus::ok) {
				return 0;
			}

			System::CriticalSection(true);
			PushStaged(StagedIn{ sourceGenerator, static_cast<uint32>(count), true });
			System::CriticalSection(false);
			return count;
		}


		// The caller holds the critical section, because the section does not nest.
		void PushStaged(const StagedIn& record) {
			SystemAssert(stagedCount < VendorBulkFunctionBase::InStageCount);
			staged[stagedCount] = record;
			stagedCount++;
		}


		// The caller holds the critical section. Returns the oldest stage, which is the one the base commits.
		StagedIn PopStaged() {
			SystemAssert(stagedCount > 0);
			StagedIn oldest = staged[0];
			for (uint8 index = 1; index < stagedCount; index++) {
				staged[index - 1] = staged[index];
			}

			stagedCount--;
			return oldest;
		}


		// The caller holds the critical section.
		bool HasStagedLoopback() const {
			for (uint8 index = 0; index < stagedCount; index++) {
				if (staged[index].fromLoopback) {
					return true;
				}
			}

			return false;
		}


		// Gives every outstanding stage back. The source generator goes back to the start of the oldest source stage, and the loopback
		// marks are released. producerGeneration changes, so a fill that began before this call is discarded.
		void RestoreStaged() {
			bool restoredSource = false;
			bool releaseLoopback = false;
			System::CriticalSection(true);
			for (uint8 index = 0; index < stagedCount; index++) {
				if (staged[index].fromLoopback) {
					releaseLoopback = true;
				} else if (!restoredSource) {
					sourceGenerator = staged[index].sourceStart;
					restoredSource = true;
				}
			}

			stagedCount = 0;
			producerGeneration++;
			System::CriticalSection(false);

			// The ring takes its own critical section, so its marks are released outside this one.
			if (releaseLoopback) {
				loopback.RollbackReads();
			}
		}


		// Checks the received bytes in chunks of one packet. The expected bytes of a chunk come from Fill(), so the compare is a tight
		// loop over a stack buffer. Only a chunk that differs takes the slow path, which counts each wrong byte.
		void CheckPattern(std::span<const uint8> data) {
			std::array<uint8, BenchProtocol::BulkPacketSize> expected{};
			size_t position = 0;
			while (position < data.size()) {
				size_t count = data.size() - position;
				if (count > expected.size()) {
					count = expected.size();
				}

				std::span<uint8> window(expected.data(), count);
				sinkGenerator.Fill(window);
				std::span<const uint8> received = data.subspan(position, count);
				if (!std::equal(received.begin(), received.end(), window.begin())) {
					CountMismatches(received, window, sinkOffset + position);
				}

				position += count;
			}

			sinkOffset += data.size();
		}


		// The slow path of CheckPattern(). firstOffset is the stream offset of received[0].
		void CountMismatches(std::span<const uint8> received, std::span<const uint8> expected, uint64 firstOffset) {
			for (size_t index = 0; index < received.size(); index++) {
				if (received[index] != expected[index]) {
					counters.receiveErrors++;

					// firstErrorOffset is 32 bits wide. Past 4 GiB of checked data per SetMode it is not recorded, so after that it
					// stays NoErrorOffset even when errors occur.
					uint64 offset = firstOffset + index;
					if (counters.firstErrorOffset == NoErrorOffset && offset < NoErrorOffset) {
						counters.firstErrorOffset = static_cast<uint32>(offset);
					}
				}
			}
		}
	};
}
