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
	// How long a run with a payload given up on waits for the board's last echoes. The board gives an echo up after four sends of
	// 200 ms and more each, so its echoes still on their way arrive well inside this time.
	constexpr std::chrono::seconds EchoSettle(3);


	struct Options {
		std::string link;
		std::string port;
		std::size_t count = 200;
		uint32 seed = 0;
		bool seedGiven = false;
		std::size_t loss = 0;
		std::size_t timeoutSeconds = 120;
		std::size_t inflight = 4;
		bool requests = false;
	};


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
			"             once, the replies compared, the interims checked in order, and the device's counters compared at the end.\n"
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
		bool outcomesSeen = false;
		std::chrono::steady_clock::time_point outcomesTime = start;
		while (true) {
			// A payload this side gave up on (retryExhausted) never comes back, so it is no longer in flight. A payload that failed here
			// but reached the board may still be echoed and is then counted twice, which lets at most one payload more go out.
			std::size_t done = check.Echoed() + outcomesFailed;
			if (nextSend < check.Count() && MaySend(nextSend, done, options.inflight)) {
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
			// The run is over when every payload has its outcome and every echo is in. With a payload given up on, its echo may never come,
			// so the run ends EchoSettle after the last outcome: the board's echoes still on their way (its own retries) arrive in that
			// time, and the run fails whatever comes after.
			bool outcomesIn = nextSend == check.Count() && outcomesOk + outcomesFailed == check.Count();
			if (outcomesIn && !outcomesSeen) {
				outcomesSeen = true;
				outcomesTime = std::chrono::steady_clock::now();
			}

			bool settled = outcomesFailed > 0 && outcomesSeen && std::chrono::steady_clock::now() - outcomesTime >= EchoSettle;
			bool finished = outcomesIn && (check.Echoed() == check.Count() || settled);
			if (finished || std::chrono::steady_clock::now() >= deadline) {
				break;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		double rate = seconds > 0.0 ? static_cast<double>(check.Echoed()) / seconds : 0.0;

		// The board's last echoes wait for this side's acknowledgements. A short drain sends them before the program ends, so the board
		// does not count those echoes as given up. It is not part of the measured time.
		std::chrono::steady_clock::time_point finish = std::chrono::steady_clock::now();
		while (std::chrono::steady_clock::now() - finish < std::chrono::milliseconds(300)) {
			stack.Process();
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		received.clear();
		bool pass = sendError == ResultStatus::ok && check.Echoed() == check.Count() && check.Mismatched() == 0 && check.Duplicate() == 0 &&
			check.Unknown() == 0 && outcomesFailed == 0 && outcomesOk == check.Count();

		if (sendError != ResultStatus::ok) {
			std::fprintf(stderr, "vubcheck: the link refused a payload with status %d\n", static_cast<int>(sendError));
		}

		std::printf(
			"vubcheck link=%s sent=%zu echoed=%zu ok=%zu failed=%zu retried=%zu dropped=%zu mismatched=%zu duplicate=%zu unknown=%zu "
			"restarts=%zu rate=%.2f/s seconds=%.1f inflight=%zu seed=%lu result=%s\n",
			options.link.c_str(), check.Count(), check.Echoed(), outcomesOk, outcomesFailed, retried, static_cast<std::size_t>(link.Dropped()),
			check.Mismatched(), check.Duplicate(), check.Unknown(), restarts, rate, seconds, options.inflight,
			static_cast<unsigned long>(seed), pass ? "PASS" : "FAIL");
		if (!pass && check.Echoed() < check.Count()) {
			std::printf(
				"board: %zu echo(s) missing. Each one is an echo the board dropped (echoDropped in vubStatus: its echo queue of four was "
				"full) or one it gave up on (outcomesFailed in vubStatus: retryExhausted for the echo on its side). A payload this side "
				"gave up on (failed above) may never have reached the board, and then shows no board counter. The guide has the SWD read.\n",
				check.Count() - check.Echoed());
		}

		return pass ? 0 : 1;
	}


	// The requests check of the options, with the seed the echo check uses: the given one, or one from the random device.
	template<typename StackType, typename LinkType>
	int RunRequestsOf(StackType& stack, LinkType& link, const Options& options) {
		std::random_device device;
		uint32 seed = options.seedGiven ? options.seed : device();
		return RunRequests(stack, link, options.link.c_str(), options.count, options.inflight, options.loss, options.timeoutSeconds, seed);
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
