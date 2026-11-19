#include "Commands.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>

#include "BenchProtocol.h"
#include "BulkLane.h"
#include "Text.h"

namespace {

// Time allowed after the reconnect request, on top of the requested pull-up time, for the device to come back.
constexpr std::uint64_t ReconnectGraceMs = 15000;
constexpr std::uint64_t ReconnectPollMs = 10;

// The device counters, decoded from their little-endian wire format (BenchProtocol::Counters).
struct DeviceCounters {
	std::uint64_t bytesSent = 0;
	std::uint64_t bytesReceived = 0;
	std::uint32_t receiveErrors = 0;
	std::uint32_t firstErrorOffset = 0;
	std::uint32_t busResets = 0;
	std::uint32_t usbErrors = 0;
};


std::uint64_t ReadLittleEndian(const std::uint8_t* data, std::size_t size) {
	std::uint64_t value = 0;
	for (std::size_t index = 0; index < size; index++) {
		value |= static_cast<std::uint64_t>(data[index]) << (8 * index);
	}

	return value;
}


bool ReadCounters(IUsbBackend& backend, DeviceCounters& counters, std::string& error) {
	std::array<std::uint8_t, BenchProtocol::CountersSize> bytes {};
	std::size_t received = 0;
	if (!backend.ControlIn(BenchProtocol::Request::GetCounters, 0, bytes, received, error)) {
		return false;
	}

	if (received != BenchProtocol::CountersSize) {
		AppendFormat(
			error,
			"the counters reply has %u bytes, expected %u",
			static_cast<unsigned int>(received),
			static_cast<unsigned int>(BenchProtocol::CountersSize));
		return false;
	}

	counters.bytesSent = ReadLittleEndian(bytes.data() + 0, 8);
	counters.bytesReceived = ReadLittleEndian(bytes.data() + 8, 8);
	counters.receiveErrors = static_cast<std::uint32_t>(ReadLittleEndian(bytes.data() + 16, 4));
	counters.firstErrorOffset = static_cast<std::uint32_t>(ReadLittleEndian(bytes.data() + 20, 4));
	counters.busResets = static_cast<std::uint32_t>(ReadLittleEndian(bytes.data() + 24, 4));
	counters.usbErrors = static_cast<std::uint32_t>(ReadLittleEndian(bytes.data() + 28, 4));
	return true;
}


void AppendCounters(std::string& text, const DeviceCounters& counters) {
	AppendFormat(
		text,
		"Device counters: sent %llu bytes, received %llu bytes, receive errors %u",
		static_cast<unsigned long long>(counters.bytesSent),
		static_cast<unsigned long long>(counters.bytesReceived),
		static_cast<unsigned int>(counters.receiveErrors));
	if (counters.firstErrorOffset == 0xFFFFFFFF) {
		text += ", first error none";
	} else {
		AppendFormat(text, ", first error at offset %u", static_cast<unsigned int>(counters.firstErrorOffset));
	}

	AppendFormat(
		text,
		", bus resets %u, USB errors %u\n",
		static_cast<unsigned int>(counters.busResets),
		static_cast<unsigned int>(counters.usbErrors));
}


bool WantsIn(Command command) {
	return command == Command::In || command == Command::Loopback || command == Command::SourceSink;
}


bool WantsOut(Command command) {
	return command == Command::Out || command == Command::Loopback || command == Command::SourceSink;
}


BenchProtocol::Mode ModeFor(Command command) {
	switch (command) {
		case Command::In:
			return BenchProtocol::Mode::Source;
		case Command::Out:
			return BenchProtocol::Mode::Sink;
		case Command::Loopback:
			return BenchProtocol::Mode::Loopback;
		case Command::SourceSink:
			return BenchProtocol::Mode::SourceAndSink;
		default:
			break;
	}

	return BenchProtocol::Mode::Idle;
}


const char* CommandName(Command command) {
	switch (command) {
		case Command::In:
			return "in";
		case Command::Out:
			return "out";
		case Command::Loopback:
			return "loopback";
		case Command::SourceSink:
			return "sourcesink";
		default:
			break;
	}

	return "";
}


// SetMode resets the device's generators and counters and stops a stream a previous run left running. The drain then
// discards data the device had already queued for that stream, so the pattern check starts at the first byte.
bool PrepareDevice(IUsbBackend& backend, BenchProtocol::Mode mode, std::string& error) {
	std::uint16_t idle = static_cast<std::uint16_t>(BenchProtocol::Mode::Idle);
	if (!backend.ControlOut(BenchProtocol::Request::SetMode, idle, error)) {
		return false;
	}

	if (!backend.DrainBulkIn(error)) {
		return false;
	}

	return backend.ControlOut(BenchProtocol::Request::SetMode, static_cast<std::uint16_t>(mode), error);
}


void AppendThroughput(std::string& text, const char* label, std::uint64_t bytes, double seconds) {
	double safeSeconds = seconds > 0.0 ? seconds : 1e-9;
	double megabytesPerSecond = static_cast<double>(bytes) / 1000000.0 / safeSeconds;
	double mebibytesPerSecond = static_cast<double>(bytes) / 1048576.0 / safeSeconds;
	AppendFormat(text, "%s throughput: %.3f MB/s (%.3f MiB/s)\n", label, megabytesPerSecond, mebibytesPerSecond);
}


void AppendLaneCheck(std::string& text, const char* label, const BulkLane& lane) {
	AppendFormat(
		text,
		"%s check: %llu bytes, %llu mismatched",
		label,
		static_cast<unsigned long long>(lane.GetVerifiedBytes()),
		static_cast<unsigned long long>(lane.GetMismatchBytes()));
	if (lane.GetMismatchBytes() > 0) {
		AppendFormat(text, " (first at stream offset %llu)", static_cast<unsigned long long>(lane.GetFirstMismatchOffset()));
	}

	text += "\n";
}


int RunInfo(IUsbBackend& backend) {
	std::string error;
	if (!backend.Open(error)) {
		std::printf("%s\n", error.c_str());
		return ExitDevice;
	}

	std::string text;
	bool described = backend.Describe(text, error);
	backend.Close();
	if (!described) {
		std::printf("%s\n", error.c_str());
		return ExitDevice;
	}

	std::fputs(text.c_str(), stdout);
	return ExitOk;
}


int RunCounters(IUsbBackend& backend) {
	std::string error;
	if (!backend.Open(error)) {
		std::printf("%s\n", error.c_str());
		return ExitDevice;
	}

	DeviceCounters counters;
	bool read = ReadCounters(backend, counters, error);
	backend.Close();
	if (!read) {
		std::printf("%s\n", error.c_str());
		return ExitDevice;
	}

	std::string text;
	AppendCounters(text, counters);
	std::fputs(text.c_str(), stdout);
	return ExitOk;
}


int RunReconnect(IUsbBackend& backend, const Options& options) {
	std::string error;
	if (!backend.Open(error)) {
		std::printf("%s\n", error.c_str());
		return ExitDevice;
	}

	const std::chrono::steady_clock::time_point requestTime = std::chrono::steady_clock::now();
	const std::chrono::steady_clock::time_point deadline =
		requestTime + std::chrono::milliseconds(options.reconnectMs + ReconnectGraceMs);
	bool requested = backend.ControlOut(BenchProtocol::Request::Reconnect, static_cast<std::uint16_t>(options.reconnectMs), error);
	backend.Close();
	if (!requested) {
		std::printf("%s\n", error.c_str());
		return ExitDevice;
	}

	// Wait for the device to drop off the bus first. Opening it again before that would just find the old enumeration.
	bool gone = false;
	while (std::chrono::steady_clock::now() < deadline) {
		std::this_thread::sleep_for(std::chrono::milliseconds(ReconnectPollMs));
		if (!backend.Open(error)) {
			gone = true;
			break;
		}

		backend.Close();
	}

	if (!gone) {
		std::printf("the device did not disconnect after the reconnect request (fixed pull-up on this board?)\n");
		return ExitDevice;
	}

	bool back = false;
	while (std::chrono::steady_clock::now() < deadline) {
		if (backend.Open(error)) {
			back = true;
			break;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(ReconnectPollMs));
	}

	if (!back) {
		std::printf("the device did not come back within %llu ms: %s\n", static_cast<unsigned long long>(options.reconnectMs + ReconnectGraceMs), error.c_str());
		return ExitDevice;
	}

	std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - requestTime;
	DeviceCounters counters;
	bool read = ReadCounters(backend, counters, error);
	backend.Close();

	std::string text;
	AppendFormat(
		text,
		"Reconnect: pull-up off for %llu ms, device back after %.1f ms (request to enumerated again)\n",
		static_cast<unsigned long long>(options.reconnectMs),
		elapsed.count());
	if (!read) {
		AppendFormat(text, "%s\n", error.c_str());
		std::fputs(text.c_str(), stdout);
		return ExitDevice;
	}

	AppendCounters(text, counters);
	std::fputs(text.c_str(), stdout);
	return ExitOk;
}


int RunTransfer(IUsbBackend& backend, const Options& options) {
	std::string error;
	if (!backend.Open(error)) {
		std::printf("%s\n", error.c_str());
		return ExitDevice;
	}

	BenchProtocol::Mode mode = ModeFor(options.command);
	bool wantIn = WantsIn(options.command);
	bool wantOut = WantsOut(options.command);

	if (!PrepareDevice(backend, mode, error)) {
		backend.Close();
		std::printf("%s\n", error.c_str());
		return ExitDevice;
	}

	// A lane that is not used gets zero bytes, so it never submits a transfer.
	BulkLane inLane(BulkDirection::In, wantIn ? options.bytes : 0, options.transferSize, options.inflight);
	BulkLane outLane(BulkDirection::Out, wantOut ? options.bytes : 0, options.transferSize, options.inflight);

	const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	bool runOk = backend.RunBulk(wantIn ? &inLane : nullptr, wantOut ? &outLane : nullptr, error);
	const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
	std::chrono::duration<double> elapsed = end - start;

	DeviceCounters counters;
	std::string counterError;
	bool countersOk = ReadCounters(backend, counters, counterError);
	backend.Close();

	std::string text;
	AppendFormat(
		text,
		"Backend %s, device %04X:%04X, interface %u\n",
		BackendName(options.backend),
		static_cast<unsigned int>(BenchProtocol::VendorId),
		static_cast<unsigned int>(BenchProtocol::ProductId),
		static_cast<unsigned int>(BenchProtocol::InterfaceNumber));
	AppendFormat(
		text,
		"Run: %s %llu bytes, %u transfers in flight of %llu bytes, %.3f s\n",
		CommandName(options.command),
		static_cast<unsigned long long>(options.bytes),
		static_cast<unsigned int>(options.inflight),
		static_cast<unsigned long long>(options.transferSize),
		elapsed.count());

	if (wantIn) {
		AppendThroughput(text, "IN ", inLane.GetVerifiedBytes(), elapsed.count());
		AppendLaneCheck(text, "IN ", inLane);
	}

	if (wantOut) {
		AppendThroughput(text, "OUT", outLane.GetVerifiedBytes(), elapsed.count());
	}

	bool dataOk = true;
	if (wantIn && inLane.GetMismatchBytes() > 0) {
		dataOk = false;
	}

	if (countersOk) {
		AppendCounters(text, counters);
		if (counters.receiveErrors > 0) {
			dataOk = false;
		}

		if (wantOut && counters.bytesReceived != options.bytes) {
			AppendFormat(
				text,
				"device counted %llu bytes received, expected %llu\n",
				static_cast<unsigned long long>(counters.bytesReceived),
				static_cast<unsigned long long>(options.bytes));
			dataOk = false;
		}

		if (wantIn && counters.bytesSent < options.bytes) {
			AppendFormat(
				text,
				"device counted %llu bytes sent, fewer than the %llu read\n",
				static_cast<unsigned long long>(counters.bytesSent),
				static_cast<unsigned long long>(options.bytes));
			dataOk = false;
		}
	} else {
		AppendFormat(text, "%s\n", counterError.c_str());
	}

	if (!runOk) {
		AppendFormat(text, "Transport error: %s\n", error.c_str());
	}

	int exitCode = ExitOk;
	if (!runOk || !countersOk) {
		text += "Result: FAIL (transport)\n";
		exitCode = ExitDevice;
	} else if (!dataOk) {
		text += "Result: FAIL (data errors)\n";
		exitCode = ExitData;
	} else {
		text += "Result: PASS\n";
	}

	std::fputs(text.c_str(), stdout);
	return exitCode;
}

}


int RunCommand(IUsbBackend& backend, const Options& options) {
	switch (options.command) {
		case Command::Info:
			return RunInfo(backend);
		case Command::Counters:
			return RunCounters(backend);
		case Command::Reconnect:
			return RunReconnect(backend, options);
		case Command::In:
		case Command::Out:
		case Command::Loopback:
		case Command::SourceSink:
			return RunTransfer(backend, options);
		case Command::None:
			break;
	}

	return ExitUsage;
}
