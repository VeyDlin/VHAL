#pragma once
#include "LinkFaults.h"
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <Utilities/VSTD/utility/Delegate.h>
#include <array>
#include <cstddef>
#include <span>


// A FIFO of bytes of Capacity bytes. One endpoint pushes into it, the other pops from it, so two endpoints share one queue for each
// direction. A push takes as many bytes as there is room for, which is how a full USB ring behaves.
template<std::size_t Capacity>
class ByteQueue {
public:
	std::size_t Count() const {
		return count;
	}


	std::size_t Push(std::span<const uint8> bytes) {
		std::size_t taken = 0;
		while (taken < bytes.size() && count < Capacity) {
			std::size_t slot = (head + count) % Capacity;
			data[slot] = bytes[taken];
			count++;
			taken++;
		}

		return taken;
	}


	std::size_t Pop(std::span<uint8> bytes) {
		std::size_t taken = 0;
		while (taken < bytes.size() && count > 0) {
			bytes[taken] = data[head];
			head = (head + 1) % Capacity;
			count--;
			taken++;
		}

		return taken;
	}


private:
	std::array<uint8, Capacity> data{};
	std::size_t head = 0;
	std::size_t count = 0;
};


// Puts bytes on a queue through a loss schedule. A dropped byte is taken, as a wire takes it, and never queued; a flipped one is queued
// with a bit changed. Stops when the queue is full, and returns how many bytes it took.
template<std::size_t Capacity>
std::size_t PushThroughFaults(ByteQueue<Capacity>& queue, LinkFaults& faults, std::span<const uint8> bytes) {
	std::size_t taken = 0;
	while (taken < bytes.size() && queue.Count() < Capacity) {
		uint8 value = bytes[taken];
		if (faults.Byte(value) != LinkFaults::Fate::drop) {
			queue.Push(std::span<const uint8>(&value, 1));
		}

		taken++;
	}

	return taken;
}


// The members of CdcAcmFunction that a link uses: Write (count queued, 0 when not configured), Read (count copied, 0 when not
// configured), IsConfigured and IsTerminalOpen (DTR).
template<std::size_t Capacity>
class FakeCdc {
public:
	LinkFaults faults;


	FakeCdc(ByteQueue<Capacity>& outgoingQueue, ByteQueue<Capacity>& incomingQueue) : outgoing(&outgoingQueue), incoming(&incomingQueue) {
	}


	uint16 Write(std::span<const uint8> data) {
		if (!configured) {
			return 0;
		}

		return static_cast<uint16>(PushThroughFaults(*outgoing, faults, data));
	}


	uint16 Read(std::span<uint8> data) {
		if (!configured) {
			return 0;
		}

		return static_cast<uint16>(incoming->Pop(data));
	}


	bool IsConfigured() const {
		return configured;
	}


	bool IsTerminalOpen() const {
		return terminalOpen;
	}


	void SetConfigured(bool value) {
		configured = value;
	}


	void SetTerminalOpen(bool value) {
		terminalOpen = value;
	}


	// The session counter, as CdcAcmFunction::GetSessionGeneration gives it.
	uint32 GetSessionGeneration() const {
		return generation;
	}


	// A session that ends and starts again, as a bus reset or a deconfigure followed by a configure does.
	void StartNewSession() {
		generation++;
	}


private:
	ByteQueue<Capacity>* outgoing;
	ByteQueue<Capacity>* incoming;
	bool configured = false;
	bool terminalOpen = false;
	uint32 generation = 0;
};


// The members of VendorBulkFunction (buffered mode) that a link uses: Write, Read and IsConfigured. It has no DTR.
template<std::size_t Capacity>
class FakeBulk {
public:
	LinkFaults faults;


	FakeBulk(ByteQueue<Capacity>& outgoingQueue, ByteQueue<Capacity>& incomingQueue) : outgoing(&outgoingQueue), incoming(&incomingQueue) {
	}


	uint16 Write(std::span<const uint8> data) {
		if (!configured) {
			return 0;
		}

		return static_cast<uint16>(PushThroughFaults(*outgoing, faults, data));
	}


	uint16 Read(std::span<uint8> data) {
		if (!configured) {
			return 0;
		}

		return static_cast<uint16>(incoming->Pop(data));
	}


	bool IsConfigured() const {
		return configured;
	}


	void SetConfigured(bool value) {
		configured = value;
	}


	// The session counter, as VendorBulkFunction::GetSessionGeneration gives it.
	uint32 GetSessionGeneration() const {
		return generation;
	}


	// A session that ends and starts again, as a bus reset or a deconfigure followed by a configure does.
	void StartNewSession() {
		generation++;
	}


private:
	ByteQueue<Capacity>* outgoing;
	ByteQueue<Capacity>* incoming;
	bool configured = false;
	uint32 generation = 0;
};


// The members of HidFunction that a link uses: SendReport, which takes exactly ReportBytes bytes (invalidParameter otherwise),
// returns notReady before configuration and busy when QueueDepth reports wait, and SetOutputDelegate, which is refused once configured.
// The host side is HostTake, which pops the next input report, and DeliverOutput, which runs the output delegate as an OUT report does.
template<std::size_t ReportBytes, std::size_t QueueDepth>
class FakeHid {
public:
	using OutputDelegate = VSTD::Delegate<void(std::span<const uint8> report)>;

	// The largest report, as HidFunction::MaxReportLength gives it.
	static constexpr std::size_t MaxReportLength = ReportBytes;

	// The report lengths of HidFunction::Report(). The double has one input and one output report, both ReportBytes long, for
	// every report ID.
	struct ReportLengths {
		std::size_t InputLength(uint8) const {
			return ReportBytes;
		}


		std::size_t OutputLength(uint8) const {
			return ReportBytes;
		}
	};


	LinkFaults faults;


	std::array<uint8, ReportBytes> lastReport{};
	uint32 reportsSent = 0;


	ResultStatus SendReport(std::span<const uint8> data) {
		if (data.size() != ReportBytes) {
			return ResultStatus::invalidParameter;
		}

		if (!configured) {
			return ResultStatus::notReady;
		}

		if (count == QueueDepth) {
			return ResultStatus::busy;
		}

		std::size_t slot = (head + count) % QueueDepth;
		for (std::size_t index = 0; index < ReportBytes; index++) {
			queue[slot][index] = data[index];
			lastReport[index] = data[index];
		}

		count++;
		reportsSent++;
		return ResultStatus::ok;
	}


	// Copies the next input report into report and returns true, or returns false when none waits.
	bool HostTake(std::span<uint8> report) {
		if (count == 0) {
			return false;
		}

		for (std::size_t index = 0; index < ReportBytes && index < report.size(); index++) {
			report[index] = queue[head][index];
		}

		head = (head + 1) % QueueDepth;
		count--;
		return true;
	}


	// The host takes each input report and sends it to the other function's output, through this function's loss schedule: a dropped
	// report never arrives, and a flipped one arrives with one bit changed.
	void DeliverInputsTo(FakeHid& other) {
		std::array<uint8, ReportBytes> report{};
		while (HostTake(report)) {
			if (faults.Item(report) != LinkFaults::Fate::drop) {
				other.DeliverOutput(report);
			}
		}
	}


	// An OUT report from the host: the output delegate gets it, in interrupt context on a device.
	void DeliverOutput(std::span<const uint8> report) {
		if (outputDelegate) {
			outputDelegate(report);
		}
	}


	ResultStatus SetOutputDelegate(OutputDelegate value) {
		if (configured) {
			return ResultStatus::busy;
		}

		outputDelegate = value;
		return ResultStatus::ok;
	}


	bool HasOutputDelegate() const {
		return static_cast<bool>(outputDelegate);
	}


	ReportLengths Report() const {
		return ReportLengths();
	}


	void SetConfigured(bool value) {
		configured = value;
	}


private:
	std::array<std::array<uint8, ReportBytes>, QueueDepth> queue{};
	std::size_t head = 0;
	std::size_t count = 0;
	bool configured = false;
	OutputDelegate outputDelegate;
};
