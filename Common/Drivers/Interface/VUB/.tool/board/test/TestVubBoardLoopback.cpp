// The host loopback of vubcheck: the PC side of the board check runs against the device side of the demo (.demo/STM32G4/Application/DemoVub)
// over an in-memory UART line. The PC side is the chain and the run of vubcheck (src/Chains.h, src/RequestChains.h, src/Run.h), and its
// link drops frames as --loss does (src/LossyLink.h). The device side is the demo's own stack: the requests stack of Requests/ActuatorDevice.h
// in requests mode, and the echo stack of DemoVub.cpp (the same options, the echo queue of Echo/EchoQueue.h) in echo mode. The device runs
// on a thread of its own, and the run is the one vubcheck prints.
//
// Build with g++ (the board's include roots, and winmm for the timer period on Windows):
//   g++ -std=c++23 -Wall -Wextra -Werror -I Common -I .demo/STM32G4/Application -I Periphery -o loopback.exe TestVubBoardLoopback.cpp -lwinmm
// Run: loopback.exe echo|requests LOSS SEED...
// Each run prints the summary line of vubcheck; the last line is the total. The exit code is 0 when every run passes.
#include "../src/Run.h"
#include "../src/RequestChains.h"
#include <DemoVub/Echo/EchoQueue.h>
#include <DemoVub/Requests/ActuatorDevice.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Drivers/Interface/VUB/Transform/Crc.h>
#include <Utilities/Checksum/CRC/Preset/Crc16Modbus.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <mutex>
#include <random>
#include <span>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <timeapi.h>
#endif


namespace DemoVub {
	// The clock of the device's requests stacks, in microseconds (the board reads the cycle counter).
	uint32 ActuatorClockUs() {
		std::chrono::microseconds now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch());
		return static_cast<uint32>(now.count());
	}
}


namespace Loopback {
	// The two directions of the line, and the lock both ends take. A bytes-level line: the framing is the stacks'.
	struct Line {
		std::mutex mutex;
		std::deque<uint8> toDevice;
		std::deque<uint8> toPc;
	};


	// One end of the line. The PC end and the device end write and read opposite queues.
	class HostEnd {
	public:
		static constexpr VUB::LinkTraits traits = VUB::LinkTraits().MaxPacket(64).FullDuplex().Starts(VUB::Initiator::Either);


		HostEnd(Line& lineValue, bool deviceSide) : line(&lineValue), device(deviceSide) {
		}


		void SetStateSink(VUB::StateSink value) {
			static_cast<void>(value);
		}


		// Hands the bytes that wait to the receiver, up to its room. The bytes are copied out under the lock and fed after it, so a handler
		// that writes to the line does not wait on itself.
		void Poll(VUB::Receiver receiver) {
			std::array<uint8, 256> buffer{};
			std::size_t count = 0;
			{
				std::lock_guard<std::mutex> lock(line->mutex);
				std::deque<uint8>& incoming = device ? line->toDevice : line->toPc;
				std::size_t room = receiver.Room();
				while (!incoming.empty() && count < room && count < buffer.size()) {
					buffer[count] = incoming.front();
					incoming.pop_front();
					count++;
				}
			}

			if (count > 0) {
				receiver.Feed(std::span<const uint8>(buffer.data(), count));
			}
		}


		Result<std::size_t> Write(std::span<const uint8> bytes) {
			std::lock_guard<std::mutex> lock(line->mutex);
			std::deque<uint8>& outgoing = device ? line->toPc : line->toDevice;
			outgoing.insert(outgoing.end(), bytes.begin(), bytes.end());
			if (device) {
				for (uint8 byte : bytes) {
					deviceCurrent.push_back(byte);
					if (byte == 0) {
						deviceFrames.push_back(deviceCurrent);
						deviceCurrent.clear();
					}
				}
			}

			return bytes.size();
		}


		// The frames the device side wrote, in order; read after the device thread has stopped.
		inline static std::vector<std::vector<uint8>> deviceFrames;
		inline static std::vector<uint8> deviceCurrent;


	private:
		Line* line;
		bool device;
	};


	// The PC side's link: the board's loss wrapper over the line end, with the frames of the UART chain.
	using PcLink = VubBoard::LossyStream<HostEnd, VubBoard::CobsFrames>;


	// The frames the PC side wrote, in order, each with its zero byte. The loss wrapper drops frame n when n is a multiple of the loss, so the
	// trace shows which frames went and which packet numbers they carried.
	inline std::vector<std::vector<uint8>> framesWritten;
	inline bool traceOn = false;


	// The PC side's link as the stack sees it: the loss wrapper, with the frames recorded on the way.
	class TapLink {
	public:
		static constexpr VUB::LinkTraits traits = PcLink::traits;


		explicit TapLink(PcLink& innerLink) : inner(&innerLink) {
		}


		void SetDropEvery(std::size_t value) {
			inner->SetDropEvery(value);
		}


		std::size_t Dropped() const {
			return inner->Dropped();
		}


		void SetStateSink(VUB::StateSink value) {
			inner->SetStateSink(value);
		}


		void Poll(VUB::Receiver receiver) {
			inner->Poll(receiver);
		}


		Result<std::size_t> Write(std::span<const uint8> bytes) {
			for (uint8 byte : bytes) {
				current.push_back(byte);
				if (byte == 0) {
					framesWritten.push_back(current);
					current.clear();
				}
			}

			return inner->Write(bytes);
		}


	private:
		PcLink* inner;
		std::vector<uint8> current;
	};


	// Lists the PC's data packets that no acknowledgement of the device side names: each with the attempts the PC side made for it and the
	// attempts the loss dropped. Only the acknowledgements of the two-byte sequence layout are read (flags 0x02, 0x03 and the bare 0x12).
	void PrintUnacked(std::size_t loss) {
		std::map<unsigned, bool> acked;
		for (const std::vector<uint8>& frame : HostEnd::deviceFrames) {
			std::vector<uint8> body = VubBoard::CobsFrames::Body(frame);
			if (body.size() < 4) {
				continue;
			}

			uint8 flags = body[0];
			if (flags == 0x02 || flags == 0x12) {
				acked[static_cast<unsigned>(body[2]) | (static_cast<unsigned>(body[3]) << 8)] = true;
			} else if (flags == 0x03 && body.size() >= 8) {
				acked[static_cast<unsigned>(body[6]) | (static_cast<unsigned>(body[7]) << 8)] = true;
			}
		}

		std::map<unsigned, std::size_t> attempts;
		std::map<unsigned, std::size_t> dropped;
		for (std::size_t index = 0; index < framesWritten.size(); index++) {
			std::vector<uint8> body = VubBoard::CobsFrames::Body(framesWritten[index]);
			if (body.size() < 4 || (body[0] != 0x01 && body[0] != 0x03)) {
				continue;
			}

			unsigned number = static_cast<unsigned>(body[2]) | (static_cast<unsigned>(body[3]) << 8);
			attempts[number]++;
			if (loss > 0 && (index + 1) % loss == 0) {
				dropped[number]++;
			}
		}

		std::size_t unacked = 0;
		for (const std::pair<const unsigned, std::size_t>& entry : attempts) {
			if (acked.count(entry.first) == 0) {
				unacked++;
				std::printf("unacked packet=%u attempts=%zu dropped=%zu\n", entry.first, entry.second, dropped[entry.first]);
			}
		}

		std::printf("unacked packets=%zu of %zu data packets; device acknowledgements read=%zu\n", unacked, attempts.size(), acked.size());
	}


	// Prints one line per frame the PC side wrote: its number, whether the loss dropped it, and for a sequenced packet its flags, tag and number.
	void PrintTrace(std::size_t loss) {
		for (std::size_t index = 0; index < framesWritten.size(); index++) {
			std::size_t number = index + 1;
			bool dropped = loss > 0 && number % loss == 0;
			std::vector<uint8> body = VubBoard::CobsFrames::Body(framesWritten[index]);
			if (body.size() < 3 + 2) {
				std::printf("frame %zu dropped=%d bytes=%zu\n", number, dropped ? 1 : 0, body.size());
				continue;
			}

			uint8 flags = body[0];
			if ((flags & 0x01) != 0) {
				unsigned packet = static_cast<unsigned>(body[2]) | (static_cast<unsigned>(body[3]) << 8);
				std::printf("frame %zu dropped=%d flags=%02x tag=%02x packet=%u\n", number, dropped ? 1 : 0, flags, body[1], packet);
			} else {
				std::printf("frame %zu dropped=%d flags=%02x\n", number, dropped ? 1 : 0, flags);
			}
		}
	}


	// The device's echo stack of DemoVub.cpp (the UART echo: Reliable with the device's options, CRC-16/MODBUS, COBS). Its outcome and restart
	// handlers only count, so they are left out here.
	inline DemoVub::EchoQueue<32, 4> echoQueue;


	void ReceiveEcho(const VUB::Meta&, std::span<const uint8> payload) {
		static_cast<void>(echoQueue.Push(payload));
	}


	template<typename LinkType>
	struct EchoStackOf {
		using Type = VUB::Stack<+[] {
			return VUB::Builder()
				.Layer<VUB::Reliable>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(32)
				.Transform<VUB::Crc<Checksum::Crc16Modbus>>()
				.Framing<VUB::Cobs>()
				.Link<LinkType>()
				.MaxPayload(32)
				.template OnReceive<&ReceiveEcho>()
				.template Clock<&DemoVub::ActuatorClockUs>();
		}>;
	};


	// The device's main loop, on its thread: the stack's Process, the sweeps' Tick (requests) and the echoes (echo) until stop is set.
	template<typename StackType>
	void EchoLoop(StackType& stack, const std::atomic<bool>& stop) {
		while (!stop.load()) {
			stack.Process();
			while (echoQueue.Count() > 0) {
				ResultStatus status = stack.Send(echoQueue.Front());
				if (status == ResultStatus::busy) {
					break;
				}

				echoQueue.Pop();
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}


	// The device's main loop of the requests mode, as DemoVub.cpp runs it for the UART stack: the sweeps, the stack, then the events.
	template<typename StackType>
	void RequestsLoop(StackType& stack, const std::atomic<bool>& stop) {
		std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
		while (!stop.load()) {
			uint32 nowMs = static_cast<uint32>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
			DemoVub::Tick(nowMs);
			stack.Process();
			DemoVub::ServeEvents<DemoVub::UartStackIndex>(stack, nowMs);
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}


	// Clears what the runs keep between them: the PC's handler records and the device's actuator state.
	void ResetRuns() {
		framesWritten.clear();
		HostEnd::deviceFrames.clear();
		HostEnd::deviceCurrent.clear();
		VubBoard::received.clear();
		VubBoard::outcomesOk = 0;
		VubBoard::outcomesFailed = 0;
		VubBoard::retried = 0;
		VubBoard::restarts = 0;
		VubBoard::requestUnclaimed = 0;
		echoQueue = DemoVub::EchoQueue<32, 4>();
		DemoVub::state = DemoVub::ActuatorState();
		DemoVub::ticks = {};
		for (DemoVub::SweepJob& job : DemoVub::sweeps) {
			job.active = false;
			job.reportPending = false;
		}
	}


	// One run of vubcheck over the line: the device on its thread, the PC side's run in this one. Returns the run's exit code.
	int RunOne(bool requests, std::size_t loss, uint32 seed) {
		ResetRuns();
		Line line;
		HostEnd pcEnd(line, false);
		HostEnd deviceEnd(line, true);
		PcLink pcLink(pcEnd);
		TapLink tap(pcLink);
		VubBoard::Options options;
		options.link = "uart";
		options.loss = loss;
		options.seed = seed;
		options.seedGiven = true;
		options.requests = requests;
		std::atomic<bool> stop{false};
		int code = 0;
		std::random_device random;
		uint16 deviceSession = static_cast<uint16>(random());
		if (requests) {
			DemoVub::UartActuatorStack<HostEnd>::Type device(deviceEnd);
			device.template Find<VUB::Reliable>().SetSession(deviceSession);
			VubBoard::UartRequestsChainOf<TapLink>::Type pc(tap);
			std::thread thread([&device, &stop]() {
				RequestsLoop(device, stop);
			});
			code = VubBoard::RunRequestsOf(pc, tap, options);
			stop.store(true);
			thread.join();
		} else {
			EchoStackOf<HostEnd>::Type device(deviceEnd);
			device.template Find<VUB::Reliable>().SetSession(deviceSession);
			VubBoard::UartChainOf<TapLink>::Type pc(tap);
			std::thread thread([&device, &stop]() {
				EchoLoop(device, stop);
			});
			code = VubBoard::RunStack(pc, tap, options);
			stop.store(true);
			thread.join();
		}

		if (traceOn) {
			PrintTrace(loss);
			PrintUnacked(loss);
			if (requests) {
				for (std::size_t kind = 0; kind < VubBoard::RequestsCheck::KindCount; kind++) {
					std::printf("kind=%zu sent=%zu", kind, VubBoard::RequestsCheck::tallies[kind].sent);
					for (std::size_t category = 0; category < VubBoard::RequestsCheck::CategoryCount; category++) {
						std::printf(" cat%zu=%zu", category, VubBoard::RequestsCheck::tallies[kind].byCategory[category]);
					}

					std::printf("\n");
				}
			}
		}

		return code;
	}
}


int main(int argc, char** argv) {
	if (argc < 4) {
		std::fprintf(stderr, "usage: loopback echo|requests LOSS SEED...\n");
		return 2;
	}

	std::string mode = argv[1];
	if (mode != "echo" && mode != "requests") {
		std::fprintf(stderr, "loopback: the mode is echo or requests\n");
		return 2;
	}

	std::size_t loss = static_cast<std::size_t>(std::strtoul(argv[2], nullptr, 10));
#ifdef _WIN32
	timeBeginPeriod(1);
#endif
	int failed = 0;
	int runs = 0;
	for (int index = 3; index < argc; index++) {
		if (std::string(argv[index]) == "-trace") {
			Loopback::traceOn = true;
		}
	}

	for (int index = 3; index < argc; index++) {
		if (std::string(argv[index]) == "-trace") {
			continue;
		}

		uint32 seed = static_cast<uint32>(std::strtoul(argv[index], nullptr, 10));
		if (Loopback::RunOne(mode == "requests", loss, seed) != 0) {
			failed++;
		}

		runs++;
	}

#ifdef _WIN32
	timeEndPeriod(1);
#endif
	std::printf("loopback mode=%s loss=%zu runs=%d failed=%d\n", mode.c_str(), loss, runs, failed);
	return failed == 0 ? 0 : 1;
}
