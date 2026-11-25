// vubcheck: the PC side of the VUB demo board (.demo/STM32G4, DemoVub). It opens one link to the board, runs the chain of that link with
// VUB::Reliable, sends payloads of random sizes and checks that each one comes back with the same bytes and that every delivery outcome is
// ok. It prints one summary line and exits 0 when the run passes, 1 when it fails and 2 on a usage or open error.
//
// The check is run by hand against the board. It opens a serial port, a USB interface or a HID device, so no test runs it.
#include <DemoProducts.h>
#include "Chains.h"
#include "DecimalArgument.h"
#include "EchoCheck.h"
#include "LossyLink.h"
#include "Pacing.h"
#include "RequestCheck.h"
#include "RequestChains.h"
#include "Run.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <random>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <timeapi.h>
#endif


namespace VubBoard {
	// The largest --inflight of the echo check. The board's echo queue has four slots, so a larger limit lets the queue overflow (see Pacing.h).
	constexpr std::uint64_t MaxInflight = 4;
	// The largest --inflight of the requests check: the requester's open requests, which the board's requests layer serves up to eight.
	constexpr std::uint64_t MaxRequestInflight = 8;
	// The largest --timeout: one day in seconds, so the run's deadline does not overflow.
	constexpr std::uint64_t MaxTimeoutSeconds = 86400;
	// Asks the system for a 1 ms timer period for the run. On Windows a sleep lasts the timer period, 15.6 ms by default, so the run's loop
	// would be paced by it. The period is given back when the run ends.
	class TimerPeriod {
	public:
		TimerPeriod() {
#ifdef _WIN32
			timeBeginPeriod(1);
#endif
		}


		~TimerPeriod() {
#ifdef _WIN32
			timeEndPeriod(1);
#endif
		}
	};


	void PrintUsage() {
		std::fprintf(stderr,
			"usage: vubcheck --link uart|cdc|bulk|hid [--port COMn] [--count N] [--seed N] [--loss K] [--inflight N] [--timeout S] [--requests]\n"
			"  Every number is decimal digits only: no sign, no space, no other text.\n"
			"  --link     the link of the board: uart (USART1 on COM3, COBS and CRC-16/MODBUS), cdc (the USB serial port, COBS),\n"
			"             bulk (the vendor bulk interface, by its interface GUID, LengthPrefix<2>), hid (the vendor HID interface, by\n"
			"             VID 1209 and PID 0007 and the usage page 0xFF00, no framing)\n"
			"  --port     the COM port: COM3 for uart, the COM port of the USB serial port for cdc. Not used for bulk or hid\n"
			"  --count    payloads to send (or requests with --requests), 1 to 65536, default 200\n"
			"  --seed     seed of the payload sizes and content, 0 to 4294967295, default from the random device (it is printed)\n"
			"  --loss     drop every K-th outgoing frame or packet on this side, to exercise the resends; 0 (none, the default) or more\n"
			"  --inflight payloads sent and not yet echoed at once, 1 to 4, default 4 (the delivery layer's window). Above 4 the\n"
			"             board's echo queue, which has four slots, overflows and drops echoes. With --requests, requests open at\n"
			"             once, 1 to 8\n"
			"  --timeout  seconds to wait for all echoes and outcomes, 1 to 86400 (one day), default 120\n"
			"  --requests the requests check instead of the echo check: random requests of the actuator demo, each final counted\n"
			"             once, the replies compared, the interims checked in order, sweeps cancelled at three points, the device's\n"
			"             events and pings checked, and the device's counters compared at the end.\n"
			"             Only uart and cdc have the requests on the board (VHAL_VUB_MODE=Requests)\n");
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
				if (!ParseDecimalInto(argv[++index], EchoCheck::MaxIndex + 1, options.count)) {
					return false;
				}
			} else if (argument == "--seed" && hasValue) {
				if (!ParseDecimalInto(argv[++index], std::numeric_limits<uint32>::max(), options.seed)) {
					return false;
				}

				options.seedGiven = true;
			} else if (argument == "--loss" && hasValue) {
				if (!ParseDecimalInto(argv[++index], std::numeric_limits<std::size_t>::max(), options.loss)) {
					return false;
				}
			} else if (argument == "--inflight" && hasValue) {
				if (!ParseDecimalInto(argv[++index], MaxRequestInflight, options.inflight)) {
					return false;
				}
			} else if (argument == "--timeout" && hasValue) {
				if (!ParseDecimalInto(argv[++index], MaxTimeoutSeconds, options.timeoutSeconds)) {
					return false;
				}
			} else if (argument == "--requests") {
				options.requests = true;
			} else {
				return false;
			}
		}

		bool knownLink = options.link == "uart" || options.link == "cdc" || options.link == "bulk" || options.link == "hid";
		if (!knownLink || options.count == 0 || options.inflight == 0 || options.timeoutSeconds == 0) {
			return false;
		}

		// The echo check's limit is the echo queue's; the requests check has its own (see MaxRequestInflight).
		if (!options.requests && options.inflight > MaxInflight) {
			return false;
		}

		if (options.requests && (options.link == "bulk" || options.link == "hid")) {
			return false;
		}

		bool needsPort = options.link == "uart" || options.link == "cdc";
		return !needsPort || !options.port.empty();
	}


}


int main(int argc, char** argv) {
	using namespace VubBoard;
	TimerPeriod timerPeriod;
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
			if (options.requests) {
				UartRequestsChain stack(link);
				return RunRequestsOf(stack, link, options);
			}

			UartChain stack(link);
			return RunStack(stack, link, options);
		}

		CdcLinkType link(serial);
		if (options.requests) {
			CdcRequestsChain stack(link);
			return RunRequestsOf(stack, link, options);
		}

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
