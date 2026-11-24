#pragma once
#include "Chains.h"
#include "EchoCheck.h"
#include "Pacing.h"
#include "RequestCheck.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <string>
#include <thread>
#include <vector>


// The two runs of vubcheck: the echo check and the requests check, over one stack. The command line (main.cpp) and the host loopback
// (test/TestVubBoardLoopback.cpp) both call them, so the loopback runs the code that the board check runs.
namespace VubBoard {
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
		uint16 session = static_cast<uint16>(device());
		stack.template Find<VUB::Reliable>().SetSession(session);
		return RunRequests(stack, link, options.link.c_str(), options.count, options.inflight, options.loss, options.timeoutSeconds, seed);
	}
}
