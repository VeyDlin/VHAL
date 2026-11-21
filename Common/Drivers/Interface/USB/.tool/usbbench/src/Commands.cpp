#include "Commands.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "BenchProtocol.h"
#include "BulkLane.h"
#include "DeviceSelection.h"
#include "StartGate.h"
#include "Summary.h"
#include "Text.h"
#include "UsbBackend.h"

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

// What one transfer run produced: the single-device report, the figures for the --all summary and, for a transport
// failure, the error that caused it.
struct TransferReport {
	std::string text;
	std::string transportError;
	DeviceResult result;
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
	double mebibytesPerSecond = static_cast<double>(bytes) / 1048576.0 / safeSeconds;
	AppendFormat(
		text,
		"%s throughput: %.3f MB/s (%.3f MiB/s)\n",
		label,
		MegabytesPerSecond(bytes, seconds),
		mebibytesPerSecond);
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


// One line per device: its serial number and where it sits on the bus. A device that could not be read says why.
std::string DeviceListText(const std::vector<UsbDeviceInfo>& devices) {
	std::string text;
	for (const UsbDeviceInfo& device : devices) {
		AppendFormat(
			text,
			"  serial %-24s %s",
			device.serial.empty() ? "(unreadable)" : device.serial.c_str(),
			device.location.c_str());
		if (!device.problem.empty()) {
			AppendFormat(text, " (%s)", device.problem.c_str());
		}

		text += "\n";
	}

	return text;
}


// The rate column of the --all table. A direction the command does not use shows a dash.
std::string RateCell(bool used, double megabytesPerSecond) {
	std::string cell;
	if (used) {
		AppendFormat(cell, "%.3f", megabytesPerSecond);
	} else {
		cell = "-";
	}

	return cell;
}


int RunInfo(IUsbBackend& backend, const std::string& serial) {
	std::string error;
	if (!backend.Open(serial, error)) {
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


int RunCounters(IUsbBackend& backend, const std::string& serial) {
	std::string error;
	if (!backend.Open(serial, error)) {
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


int RunList(const Options& options, IUsbBackend& backend) {
	std::vector<UsbDeviceInfo> devices;
	std::string error;
	if (!backend.Enumerate(devices, error)) {
		std::printf("%s\n", error.c_str());
		return ExitDevice;
	}

	if (devices.empty()) {
		std::printf("%s\n", NoDeviceMessage().c_str());
		return ExitDevice;
	}

	std::string text;
	AppendFormat(
		text,
		"Backend %s, device %04X:%04X: %u attached\n",
		BackendName(options.backend),
		static_cast<unsigned int>(BenchProtocol::VendorId),
		static_cast<unsigned int>(BenchProtocol::ProductId),
		static_cast<unsigned int>(devices.size()));
	text += DeviceListText(devices);
	std::fputs(text.c_str(), stdout);
	return ExitOk;
}


// Works out which device a single-device command acts on. One attached device is used as before: the first that matches.
// With several, the command must name one with --serial. Running on the wrong board still prints a result, so the tool
// stops instead of guessing.
bool ChooseDevice(IUsbBackend& backend, const Options& options, std::string& serial, int& exitCode) {
	std::vector<UsbDeviceInfo> devices;
	std::string error;
	if (!backend.Enumerate(devices, error)) {
		std::printf("%s\n", error.c_str());
		exitCode = ExitDevice;
		return false;
	}

	switch (SelectSingleDevice(devices, options.serial, serial)) {
		case SingleSelection::Chosen:
			return true;
		case SingleSelection::NoDevice:
			std::printf("%s\n", NoDeviceMessage().c_str());
			exitCode = ExitDevice;
			return false;
		case SingleSelection::SerialNotFound:
			std::printf("%s\n", NoDeviceMessage(options.serial).c_str());
			exitCode = ExitDevice;
			return false;
		case SingleSelection::SerialShared:
			std::printf("two benchmark devices report the serial '%s'; the serial numbers must be unique\n", options.serial.c_str());
			exitCode = ExitDevice;
			return false;
		case SingleSelection::Several:
			std::fprintf(
				stderr,
				"usbbench: %u benchmark devices are attached; name one with --serial, or measure all of them with --all\n",
				static_cast<unsigned int>(devices.size()));
			std::fputs(DeviceListText(devices).c_str(), stderr);
			exitCode = ExitUsage;
			return false;
	}

	exitCode = ExitDevice;
	return false;
}


int RunReconnect(IUsbBackend& backend, const Options& options, const std::string& serial) {
	std::string error;
	if (!backend.Open(serial, error)) {
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
		if (!backend.Open(serial, error)) {
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
		if (backend.Open(serial, error)) {
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


// Runs one transfer command on a device that is already open. Fills in the report and leaves the backend open, so the
// caller closes it. Several of these may run at once, each on its own backend. With a gate (a --all run) the transfer
// starts only when every device is ready, so the devices share the same time window.
void RunOneTransfer(IUsbBackend& backend, const Options& options, TransferReport& report, StartGate* gate) {
	std::string error;
	BenchProtocol::Mode mode = ModeFor(options.command);
	bool wantIn = WantsIn(options.command);
	bool wantOut = WantsOut(options.command);

	bool prepared = PrepareDevice(backend, mode, error);
	if (!prepared) {
		report.text = error + "\n";
		report.transportError = error;
		report.result.verdict = RunVerdict::TransportFailure;
	}

	// A device that could not be prepared still arrives at the gate, so it never holds the other devices back.
	if (gate != nullptr && !gate->ArriveAndWait()) {
		report.text = "the run was cancelled before this device started\n";
		report.transportError = "cancelled before the transfer started";
		report.result.verdict = RunVerdict::TransportFailure;
		return;
	}

	if (!prepared) {
		return;
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

	RunVerdict verdict = JudgeRun(runOk, countersOk, dataOk);
	if (verdict == RunVerdict::TransportFailure) {
		text += "Result: FAIL (transport)\n";
	} else if (verdict == RunVerdict::DataFailure) {
		text += "Result: FAIL (data errors)\n";
	} else {
		text += "Result: PASS\n";
	}

	report.text = text;
	report.result.inMegabytesPerSecond = wantIn ? MegabytesPerSecond(inLane.GetVerifiedBytes(), elapsed.count()) : 0.0;
	report.result.outMegabytesPerSecond = wantOut ? MegabytesPerSecond(outLane.GetVerifiedBytes(), elapsed.count()) : 0.0;
	report.result.inBytes = wantIn ? inLane.GetVerifiedBytes() : 0;
	report.result.outBytes = wantOut ? outLane.GetVerifiedBytes() : 0;
	report.result.mismatchBytes = wantIn ? inLane.GetMismatchBytes() : 0;
	report.result.receiveErrors = countersOk ? counters.receiveErrors : 0;
	report.result.timed = true;
	report.result.started = start;
	report.result.ended = end;
	report.result.verdict = verdict;
	if (!runOk) {
		report.transportError = error;
	} else if (!countersOk) {
		report.transportError = counterError;
	}
}


int RunTransfer(IUsbBackend& backend, const Options& options, const std::string& serial) {
	std::string error;
	if (!backend.Open(serial, error)) {
		std::printf("%s\n", error.c_str());
		return ExitDevice;
	}

	TransferReport report;
	RunOneTransfer(backend, options, report, nullptr);
	backend.Close();
	std::fputs(report.text.c_str(), stdout);
	return ExitCodeOf(report.result.verdict);
}


// Thread entry of a --all run. An exception that escaped before the start gate would leave the other threads waiting for
// this one, so the gate is cancelled and the device reports a transport failure instead.
void RunGatedTransfer(IUsbBackend& backend, const Options& options, TransferReport& report, StartGate& gate) {
	try {
		RunOneTransfer(backend, options, report, &gate);
	} catch (const std::exception& failure) {
		gate.Cancel();
		report.text = std::string("internal error: ") + failure.what() + "\n";
		report.transportError = failure.what();
		report.result.verdict = RunVerdict::TransportFailure;
	}
}


// Runs the transfer on every attached device at once: one thread and one backend per device. Every device is opened
// before any run starts, so a board that is missing or busy stops the whole run instead of leaving a partial one.
int RunAllDevices(const Options& options, IUsbBackend& probe) {
	std::vector<UsbDeviceInfo> devices;
	std::string error;
	if (!probe.Enumerate(devices, error)) {
		std::printf("%s\n", error.c_str());
		return ExitDevice;
	}

	switch (SelectAllDevices(devices)) {
		case AllSelection::Chosen:
			break;
		case AllSelection::NoDevice:
			std::printf("%s\n", NoDeviceMessage().c_str());
			return ExitDevice;
		case AllSelection::NoSerial:
			std::fprintf(stderr, "usbbench: --all needs every device to report a serial number, and one does not\n");
			std::fputs(DeviceListText(devices).c_str(), stderr);
			return ExitDevice;
		case AllSelection::SerialShared:
			std::printf("two benchmark devices report the same serial number; --all needs them to be unique\n");
			return ExitDevice;
	}

	std::vector<std::unique_ptr<IUsbBackend>> backends;
	for (const UsbDeviceInfo& device : devices) {
		std::unique_ptr<IUsbBackend> backend = CreateBackend(options.backend, error);
		if (!backend) {
			std::fprintf(stderr, "usbbench: %s\n", error.c_str());
			return ExitUsage;
		}

		if (!backend->Open(device.serial, error)) {
			std::printf("device %s: %s\n", device.serial.c_str(), error.c_str());
			return ExitDevice;
		}

		backends.push_back(std::move(backend));
	}

	// The gate holds every thread until all of them are ready, so the transfers start together.
	std::vector<TransferReport> reports(backends.size());
	StartGate gate(backends.size());
	std::vector<std::thread> threads;
	threads.reserve(backends.size());
	std::string startError;
	for (std::size_t index = 0; index < backends.size(); index++) {
		reports[index].result.serial = devices[index].serial;
		try {
			threads.emplace_back(
				RunGatedTransfer,
				std::ref(*backends[index]),
				std::cref(options),
				std::ref(reports[index]),
				std::ref(gate));
		} catch (const std::system_error& failure) {
			// The threads already started wait at the gate for this one. Cancelling releases them without a transfer, and
			// they are joined below before anything is reported.
			startError = "could not start a thread for device " + devices[index].serial + ": " + failure.what();
			gate.Cancel();
			break;
		}
	}

	for (std::thread& thread : threads) {
		thread.join();
	}

	for (std::unique_ptr<IUsbBackend>& backend : backends) {
		backend->Close();
	}

	if (!startError.empty()) {
		std::fprintf(stderr, "usbbench: %s\n", startError.c_str());
		return ExitDevice;
	}

	std::vector<DeviceResult> results;
	for (const TransferReport& report : reports) {
		results.push_back(report.result);
	}

	SummaryTotals totals = SumResults(results);
	RunVerdict worst = WorstVerdict(results);
	bool wantIn = WantsIn(options.command);
	bool wantOut = WantsOut(options.command);

	std::string text;
	AppendFormat(
		text,
		"Backend %s, device %04X:%04X, %u devices at once. Run: %s %llu bytes each, %u transfers in flight of %llu bytes\n",
		BackendName(options.backend),
		static_cast<unsigned int>(BenchProtocol::VendorId),
		static_cast<unsigned int>(BenchProtocol::ProductId),
		static_cast<unsigned int>(results.size()),
		CommandName(options.command),
		static_cast<unsigned long long>(options.bytes),
		static_cast<unsigned int>(options.inflight),
		static_cast<unsigned long long>(options.transferSize));
	AppendFormat(
		text,
		"%-24s %10s %10s %11s %10s  %s\n",
		"Serial",
		"IN MB/s",
		"OUT MB/s",
		"Mismatches",
		"RX errors",
		"Result");
	for (const DeviceResult& result : results) {
		AppendFormat(
			text,
			"%-24s %10s %10s %11llu %10llu  %s\n",
			result.serial.c_str(),
			RateCell(wantIn, result.inMegabytesPerSecond).c_str(),
			RateCell(wantOut, result.outMegabytesPerSecond).c_str(),
			static_cast<unsigned long long>(result.mismatchBytes),
			static_cast<unsigned long long>(result.receiveErrors),
			VerdictText(result.verdict));
	}

	// The total passes only when every device passed. Otherwise it says how many did.
	std::string overall;
	if (totals.passed == totals.devices) {
		overall = "PASS";
	} else {
		AppendFormat(
			overall,
			"FAIL (%u of %u passed)",
			static_cast<unsigned int>(totals.passed),
			static_cast<unsigned int>(totals.devices));
	}

	AppendFormat(
		text,
		"%-24s %10s %10s %11llu %10llu  %s\n",
		"total",
		RateCell(wantIn, totals.inMegabytesPerSecond).c_str(),
		RateCell(wantOut, totals.outMegabytesPerSecond).c_str(),
		static_cast<unsigned long long>(totals.mismatchBytes),
		static_cast<unsigned long long>(totals.receiveErrors),
		overall.c_str());

	// The reason for a transport failure is not in the table, so it follows the table, one line per device.
	for (const TransferReport& report : reports) {
		if (!report.transportError.empty()) {
			text += "device " + report.result.serial + ": transport error: " + report.transportError + "\n";
		}
	}

	std::fputs(text.c_str(), stdout);
	return ExitCodeOf(worst);
}

}


int RunCommand(const Options& options) {
	std::string error;
	std::unique_ptr<IUsbBackend> backend = CreateBackend(options.backend, error);
	if (!backend) {
		std::fprintf(stderr, "usbbench: %s\n", error.c_str());
		return ExitUsage;
	}

	if (options.command == Command::List) {
		return RunList(options, *backend);
	}

	if (options.allDevices) {
		return RunAllDevices(options, *backend);
	}

	std::string serial;
	int exitCode = ExitOk;
	if (!ChooseDevice(*backend, options, serial, exitCode)) {
		return exitCode;
	}

	switch (options.command) {
		case Command::Info:
			return RunInfo(*backend, serial);
		case Command::Counters:
			return RunCounters(*backend, serial);
		case Command::Reconnect:
			return RunReconnect(*backend, options, serial);
		case Command::In:
		case Command::Out:
		case Command::Loopback:
		case Command::SourceSink:
			return RunTransfer(*backend, options, serial);
		case Command::None:
		case Command::List:
			break;
	}

	return ExitUsage;
}
