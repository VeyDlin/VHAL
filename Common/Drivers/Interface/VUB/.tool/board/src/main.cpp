// vubcheck: the PC side of the VUB demo board (.demo/STM32G4, DemoVub). It opens one link to the board, runs the chain of that link with
// VUB::Reliable, sends payloads of random sizes and checks that each one comes back with the same bytes and that every delivery outcome is
// ok. It prints one summary line and exits 0 when the run passes, 1 when it fails and 2 on a usage or open error.
//
// The check is run by hand against the board. It opens a serial port, a USB interface or a HID device, so no test runs it.
#include <DemoProducts.h>
#include "Chains.h"
#include "EchoCheck.h"
#include "LossyLink.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <thread>
#include <vector>


namespace VubBoard {
	struct Options {
		std::string link;
		std::string port;
		std::size_t count = 200;
		uint32 seed = 0;
		bool seedGiven = false;
		std::size_t loss = 0;
		std::size_t timeoutSeconds = 120;
	};


	void PrintUsage() {
		std::fprintf(stderr,
			"usage: vubcheck --link uart|cdc|bulk|hid [--port COMn] [--count N] [--seed N] [--loss K] [--timeout S]\n"
			"  --link    the link of the board: uart (USART1 on COM3, COBS and CRC-16/MODBUS), cdc (the USB serial port, COBS),\n"
			"            bulk (the vendor bulk interface, by its interface GUID, LengthPrefix<2>), hid (the vendor HID interface, by\n"
			"            VID 1209 and PID 0007 and the usage page 0xFF00, no framing)\n"
			"  --port    the COM port: COM3 for uart, the COM port of the USB serial port for cdc. Not used for bulk or hid\n"
			"  --count   payloads to send, default 200\n"
			"  --seed    seed of the payload sizes and content, default from the random device (it is printed)\n"
			"  --loss    drop every K-th outgoing frame or packet on this side, to exercise the resends; default 0 (none)\n"
			"  --timeout seconds to wait for all echoes and outcomes, default 120\n");
	}


	// Reads the arguments. Returns false on a bad or missing argument, after printing nothing (the caller prints the usage).
	bool ParseOptions(int argc, char** argv, Options& options) {
		for (int index = 1; index < argc; index++) {
			std::string argument = argv[index];
			bool hasValue = index + 1 < argc;
			if (argument == "--link" && hasValue) {
				options.link = argv[++index];
			} else if (argument == "--port" && hasValue) {
				options.port = argv[++index];
			} else if (argument == "--count" && hasValue) {
				options.count = std::strtoull(argv[++index], nullptr, 10);
			} else if (argument == "--seed" && hasValue) {
				options.seed = static_cast<uint32>(std::strtoul(argv[++index], nullptr, 10));
				options.seedGiven = true;
			} else if (argument == "--loss" && hasValue) {
				options.loss = std::strtoull(argv[++index], nullptr, 10);
			} else if (argument == "--timeout" && hasValue) {
				options.timeoutSeconds = std::strtoull(argv[++index], nullptr, 10);
			} else {
				return false;
			}
		}

		bool knownLink = options.link == "uart" || options.link == "cdc" || options.link == "bulk" || options.link == "hid";
		if (!knownLink || options.count == 0 || options.count > EchoCheck::MaxIndex + 1) {
			return false;
		}

		bool needsPort = options.link == "uart" || options.link == "cdc";
		return !needsPort || !options.port.empty();
	}


	// Sends the payloads through the stack and checks the echoes, until all are echoed and all outcomes are in, or the timeout ends the run.
	// Returns the exit code.
	template<typename StackType, typename LinkType>
	int RunStack(StackType& stack, LinkType& link, const Options& options) {
		link.SetDropEvery(options.loss);
		std::random_device device;
		uint32 seed = options.seedGiven ? options.seed : device();
		uint16 session = static_cast<uint16>(device());
		stack.template Find<VUB::Reliable>().SetSession(session);

		EchoCheck check(options.count, ChainMaxPayload, seed);
		std::size_t nextSend = 0;
		ResultStatus sendError = ResultStatus::ok;
		std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
		std::chrono::steady_clock::time_point deadline = start + std::chrono::seconds(options.timeoutSeconds);
		while (true) {
			if (nextSend < check.Count()) {
				ResultStatus status = stack.Send(check.Payload(nextSend));
				if (status == ResultStatus::ok) {
					nextSend++;
				} else if (status != ResultStatus::busy) {
					sendError = status;
					break;
				}
			}

			stack.Process();
			for (const std::vector<uint8>& echo : received) {
				check.Accept(std::span<const uint8>(echo.data(), echo.size()));
			}

			received.clear();
			bool finished = nextSend == check.Count() && check.Echoed() == check.Count() && outcomesOk + outcomesFailed == check.Count();
			if (finished || std::chrono::steady_clock::now() >= deadline) {
				break;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		double rate = seconds > 0.0 ? static_cast<double>(check.Echoed()) / seconds : 0.0;
		bool pass = sendError == ResultStatus::ok && check.Echoed() == check.Count() && check.Mismatched() == 0 && check.Duplicate() == 0 &&
			check.Unknown() == 0 && outcomesFailed == 0 && outcomesOk == check.Count();

		if (sendError != ResultStatus::ok) {
			std::fprintf(stderr, "vubcheck: the link refused a payload with status %d\n", static_cast<int>(sendError));
		}

		std::printf(
			"vubcheck link=%s sent=%zu echoed=%zu ok=%zu retried=%zu dropped=%zu mismatched=%zu duplicate=%zu unknown=%zu "
			"restarts=%zu rate=%.2f/s seconds=%.1f seed=%lu result=%s\n",
			options.link.c_str(), check.Count(), check.Echoed(), outcomesOk, retried, static_cast<std::size_t>(link.Dropped()),
			check.Mismatched(), check.Duplicate(), check.Unknown(), restarts, rate, seconds, static_cast<unsigned long>(seed),
			pass ? "PASS" : "FAIL");
		return pass ? 0 : 1;
	}
}


int main(int argc, char** argv) {
	using namespace VubBoard;
	Options options;
	if (!ParseOptions(argc, argv, options)) {
		PrintUsage();
		return 2;
	}

	if (options.link == "uart" || options.link == "cdc") {
		VUB::PC::SerialLink serial;
		VUB::PC::SerialLink::Settings settings;
		if (serial.Open(options.port, settings) != ResultStatus::ok) {
			std::fprintf(stderr, "vubcheck: cannot open %s\n", options.port.c_str());
			return 2;
		}

		if (options.link == "uart") {
			UartLinkType link(serial);
			UartChain stack(link);
			return RunStack(stack, link, options);
		}

		CdcLinkType link(serial);
		CdcChain stack(link);
		return RunStack(stack, link, options);
	}

	if (options.link == "bulk") {
		VUB::PC::BulkLink bulk;
		VUB::PC::BulkLink::Settings settings;
		settings.interfaceGuid = DemoProducts::VubInterfaceGuid;
		settings.vendorId = DemoProducts::VendorId;
		settings.productId = DemoProducts::VubProductId;
		settings.interfaceNumber = DemoProducts::VubVendorBulkInterface;
		settings.inEndpoint = DemoProducts::VubVendorBulkInAddress;
		settings.outEndpoint = DemoProducts::VubVendorBulkOutAddress;
		if (bulk.Open(settings) != ResultStatus::ok) {
			std::fprintf(stderr, "vubcheck: cannot open the vendor bulk interface of %s\n", DemoProducts::VubInterfaceGuid);
			return 2;
		}

		BulkLinkType link(bulk);
		BulkChain stack(link);
		return RunStack(stack, link, options);
	}

	VUB::PC::HidLink<64> hid;
	if (hid.Open(DemoProducts::VendorId, DemoProducts::VubProductId, DemoProducts::VubHidUsagePage) != ResultStatus::ok) {
		std::fprintf(stderr, "vubcheck: cannot open the vendor HID device 1209:0007 with usage page 0xFF00\n");
		return 2;
	}

	HidLinkType link(hid);
	HidChain stack(link);
	return RunStack(stack, link, options);
}
