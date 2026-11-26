#include "ReliableChains.h"
#include "SimCheck.h"
#include <Drivers/Interface/VUB/TestKit/Loopback.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <span>
#include <vector>

// A pair of stacks with VUB::Reliable over a lossless line. One side (the PC) sends payloads, and the other side (the board) echoes
// each payload it receives. One side is then rebuilt on the same line end with another boot value, and the exchange runs again. The
// echoes after the rebuild must be delivered, without retries that run out.


// Payload ids of the sender carry no flag. The echo of a payload has the flag set, so the same handlers serve both sides.
inline constexpr uint16 RejoinEchoFlag = 0x8000;
inline constexpr std::size_t RejoinIds = 1024;
inline constexpr uint16 RejoinIndexMask = 0x7FFF;
inline constexpr std::size_t RejoinMaxRounds = 60000;
inline constexpr uint16 RejoinInFlightIds = 200;


inline std::array<uint32, RejoinIds> sendAtUs{};
inline std::array<uint32, RejoinIds> sendOutcomeAtUs{};
inline std::array<uint16, RejoinIds> sendOutcomes{};
inline std::array<ResultStatus, RejoinIds> sendStatus{};
inline std::array<uint32, RejoinIds> echoSentAtUs{};
inline std::array<uint32, RejoinIds> echoInAtUs{};
inline std::array<uint16, RejoinIds> echoesIn{};
inline std::array<uint32, RejoinIds> echoOutcomeAtUs{};
inline std::array<uint16, RejoinIds> echoOutcomes{};
inline std::array<ResultStatus, RejoinIds> echoStatus{};
inline std::array<uint16, RejoinIds> deliveredAtBoard{};
inline std::array<bool, RejoinIds> sendSent{};
inline std::array<bool, RejoinIds> echoSent{};
inline std::vector<uint16> echoQueue;
inline uint32 rejoinRestarts = 0;


inline void RejoinOutcome(const VUB::Outcome& outcome) {
	uint16 id = ReliableIdOf(outcome.payload);
	uint16 index = static_cast<uint16>(id & RejoinIndexMask);
	if (index >= RejoinIds) {
		return;
	}

	if ((id & RejoinEchoFlag) != 0) {
		echoOutcomes[index]++;
		echoStatus[index] = outcome.status;
		echoOutcomeAtUs[index] = testNowUs;
	} else {
		sendOutcomes[index]++;
		sendStatus[index] = outcome.status;
		sendOutcomeAtUs[index] = testNowUs;
	}
}


inline void RejoinReceive(const VUB::Meta&, std::span<const uint8> payload) {
	uint16 id = ReliableIdOf(payload);
	uint16 index = static_cast<uint16>(id & RejoinIndexMask);
	if (index >= RejoinIds) {
		return;
	}

	if ((id & RejoinEchoFlag) != 0) {
		echoesIn[index]++;
		echoInAtUs[index] = testNowUs;
	} else {
		deliveredAtBoard[index]++;
		echoQueue.push_back(static_cast<uint16>(id | RejoinEchoFlag));
	}
}


inline void RejoinRestart() {
	rejoinRestarts++;
}


inline void ResetRejoinState() {
	ResetTestState();
	sendAtUs.fill(0);
	sendOutcomeAtUs.fill(0);
	sendOutcomes.fill(0);
	sendStatus.fill(ResultStatus::ok);
	echoSentAtUs.fill(0);
	echoInAtUs.fill(0);
	echoesIn.fill(0);
	echoOutcomeAtUs.fill(0);
	echoOutcomes.fill(0);
	echoStatus.fill(ResultStatus::ok);
	deliveredAtBoard.fill(0);
	sendSent.fill(false);
	echoSent.fill(false);
	echoQueue.clear();
	rejoinRestarts = 0;
}


// Packet chain, the layer over a packet line with no transform.
using RejoinPacket = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<ReliableLayer>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(40).OnOutcome<&RejoinOutcome>().OnRestart<&RejoinRestart>()
		.Link<PacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&RejoinReceive>()
		.Clock<&TestClock>();
}>;


// Stream chain: the XOR check transform and the flag framing over a stream line.
using RejoinStream = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<ReliableLayer>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(40).OnOutcome<&RejoinOutcome>().OnRestart<&RejoinRestart>()
		.Transform<XorTransform>()
		.Framing<FlagFraming>()
		.Link<StreamEnd>()
		.MaxPayload(40)
		.ReceiveRing(256)
		.OnReceive<&RejoinReceive>()
		.Clock<&TestClock>();
}>;


// What a run of one phase did: the outcomes of its sends and of their echoes, the echoes the sender got, and the times, in
// microseconds of the simulated clock.
struct PhaseStats {
	uint32 firstUs = 0;
	uint32 lastUs = 0;
	uint16 sendOk = 0;
	uint16 sendReset = 0;
	uint16 sendRetry = 0;
	uint16 echoOk = 0;
	uint16 echoRetry = 0;
	uint16 echoReset = 0;
	uint16 echoFailed = 0;
	uint16 echoesIn = 0;
	uint16 echoesInTwice = 0;
	uint16 deliveredOnce = 0;
	uint32 worstRoundTripUs = 0;
	uint32 worstEchoUs = 0;
};


template<typename Line, typename Stack>
void AdvanceOneRound(Stack& pc, Stack& board, Line& line) {
	pc.Process();
	line.DeliverToB(board);
	board.Process();
	line.DeliverToA(pc);
	pc.Process();
}


// Every send has its outcome, and every send that was ok has its echo's outcome. A send that was reset is never echoed.
inline bool PhaseSettled(uint16 first, uint16 count) {
	for (uint16 index = first; index < first + count; index++) {
		if (sendOutcomes[index] != 1) {
			return false;
		}

		if (sendStatus[index] == ResultStatus::ok && echoOutcomes[index] != 1) {
			return false;
		}
	}

	return true;
}


inline PhaseStats SummarisePhase(uint16 first, uint16 count, uint32 firstUs) {
	PhaseStats stats{};
	stats.firstUs = firstUs;
	for (uint16 index = first; index < first + count; index++) {
		if (sendStatus[index] == ResultStatus::ok) {
			stats.sendOk++;
		} else if (sendStatus[index] == ResultStatus::retryExhausted) {
			stats.sendRetry++;
		} else {
			stats.sendReset++;
		}

		if (echoOutcomes[index] == 1 && echoStatus[index] == ResultStatus::ok) {
			stats.echoOk++;
		} else if (echoOutcomes[index] == 1 && echoStatus[index] == ResultStatus::retryExhausted) {
			stats.echoRetry++;
		} else if (echoOutcomes[index] == 1) {
			stats.echoReset++;
		}

		if (echoesIn[index] >= 1) {
			stats.echoesIn++;
		}

		if (echoesIn[index] >= 2) {
			stats.echoesInTwice++;
		}

		if (deliveredAtBoard[index] == 1) {
			stats.deliveredOnce++;
		}

		if (echoesIn[index] >= 1 && echoInAtUs[index] - sendAtUs[index] > stats.worstRoundTripUs) {
			stats.worstRoundTripUs = echoInAtUs[index] - sendAtUs[index];
		}

		if (echoOutcomes[index] >= 1 && echoOutcomeAtUs[index] - echoSentAtUs[index] > stats.worstEchoUs) {
			stats.worstEchoUs = echoOutcomeAtUs[index] - echoSentAtUs[index];
		}

		if (sendOutcomeAtUs[index] > stats.lastUs) {
			stats.lastUs = sendOutcomeAtUs[index];
		}

		if (echoOutcomeAtUs[index] > stats.lastUs) {
			stats.lastUs = echoOutcomeAtUs[index];
		}
	}

	return stats;
}


void PrintPhase(const char* label, const PhaseStats& stats, uint32 restarts) {
	uint32 spanUs = stats.lastUs - stats.firstUs;
	double rate = spanUs == 0 ? 0.0 : (static_cast<double>(stats.sendOk) * 1000000.0) / static_cast<double>(spanUs);
	std::printf("%s: PC sends ok %u reset %u retryExhausted %u; echo outcomes ok %u retryExhausted %u reset %u; echoes at PC %u "
		"(twice %u); board delivered once %u; worst PC round trip %u us; worst echo outcome %u us; span %u us; rate %.1f per s; "
		"restarts %u\n",
		label, stats.sendOk, stats.sendReset, stats.sendRetry, stats.echoOk, stats.echoRetry, stats.echoReset, stats.echoesIn,
		stats.echoesInTwice, stats.deliveredOnce, stats.worstRoundTripUs, stats.worstEchoUs, spanUs, rate, restarts);
}


// The sends of a round: the PC sends the next id of the phase while the layer takes it, and the board sends the echoes it has queued.
// next is the first id not yet sent, and end is the id after the phase.
template<typename Line, typename Stack>
void Launch(Line& line, Stack& pc, Stack& board, uint16& next, uint16 end, uint32& clockUs) {
	clockUs += 1000;
	testNowUs = clockUs;
	line.SetNow(clockUs);
	while (next < end && ReliableSend(pc, next) == ResultStatus::ok) {
		sendAtUs[next] = clockUs;
		sendSent[next] = true;
		next++;
	}

	while (!echoQueue.empty()) {
		uint16 id = echoQueue.front();
		if (ReliableSend(board, id) != ResultStatus::ok) {
			break;
		}

		uint16 index = static_cast<uint16>(id & RejoinIndexMask);
		echoSentAtUs[index] = clockUs;
		echoSent[index] = true;
		echoQueue.erase(echoQueue.begin());
	}
}


// One round of the exchange: the sends, then the packets are carried and each side processes.
template<typename Line, typename Stack>
void SendAndEcho(Line& line, Stack& pc, Stack& board, uint16& next, uint16 end, uint32& clockUs) {
	Launch(line, pc, board, next, end, clockUs);
	AdvanceOneRound(pc, board, line);
}


// The first half of a round: the sends go into the line and the sides process, but the line delivers nothing. The packets stay in the
// line, so a side rebuilt after the cut receives them.
template<typename Line, typename Stack>
void CutRound(Line& line, Stack& pc, Stack& board, uint16& next, uint16 end, uint32& clockUs) {
	Launch(line, pc, board, next, end, clockUs);
	pc.Process();
	board.Process();
}


// Sends the ids first to first + count - 1 from the PC side, and the board echoes each one as it arrives. Runs until every id has an
// outcome on both sides, or until the round budget is spent.
template<typename Line, typename Stack>
PhaseStats RunPhase(Line& line, Stack& pc, Stack& board, uint16 first, uint16 count, uint32& clockUs) {
	uint32 firstUs = clockUs;
	uint16 next = first;
	uint16 end = static_cast<uint16>(first + count);
	for (std::size_t round = 0; round < RejoinMaxRounds; round++) {
		SendAndEcho(line, pc, board, next, end, clockUs);
		if (next == end && PhaseSettled(first, count)) {
			break;
		}
	}

	return SummarisePhase(first, count, firstUs);
}


// Runs the sends of the phase for a number of rounds, then stops halfway through a round, so the packets sent are in the line and not
// yet delivered.
template<typename Line, typename Stack>
void RunRounds(Line& line, Stack& pc, Stack& board, uint16 first, uint16 count, uint32& clockUs, std::size_t rounds) {
	uint16 next = first;
	uint16 end = static_cast<uint16>(first + count);
	for (std::size_t round = 0; round < rounds; round++) {
		SendAndEcho(line, pc, board, next, end, clockUs);
	}

	CutRound(line, pc, board, next, end, clockUs);
}


// True when every packet the surviving side sent has its outcome. With the PC rebuilt the survivor is the board, which sent the echoes;
// with the board rebuilt it is the PC, which sent the payloads.
inline bool SurvivorSettled(bool restartPc) {
	for (uint16 index = 0; index < RejoinInFlightIds; index++) {
		if (restartPc && echoSent[index] && echoOutcomes[index] != 1) {
			return false;
		}

		if (!restartPc && sendSent[index] && sendOutcomes[index] != 1) {
			return false;
		}
	}

	return true;
}


// Runs rounds with nothing left to send until the surviving side has the outcome of every packet it sent.
template<typename Line, typename Stack>
void DrainSurvivor(Line& line, Stack& pc, Stack& board, bool restartPc, uint32& clockUs) {
	uint16 next = 0;
	for (std::size_t round = 0; round < RejoinMaxRounds; round++) {
		SendAndEcho(line, pc, board, next, 0, clockUs);
		if (SurvivorSettled(restartPc)) {
			return;
		}
	}
}


// Builds the pair, runs a phase, rebuilds one side with another boot value, and runs a second phase with the next ids. The side that is
// rebuilt is the PC when restartPc is set, and the board otherwise. The line is a packet or a stream line, by its type.
template<typename Line, typename Stack>
void Scenario(const char* name, bool restartPc) {
	ResetRejoinState();
	Line line(VUB::Faults{});
	std::optional<Stack> pc;
	std::optional<Stack> board;
	pc.emplace(line.a);
	board.emplace(line.b);
	GiveSession(*pc, 0x1234);
	GiveSession(*board, 0x5A5A);
	uint32 clockUs = 0x0100;
	testNowUs = clockUs;
	line.SetNow(clockUs);
	pc->Process();
	board->Process();

	PhaseStats first = RunPhase(line, *pc, *board, 0, 100, clockUs);
	char label[160];
	std::snprintf(label, sizeof(label), "%s before restart", name);
	PrintPhase(label, first, rejoinRestarts);

	if (restartPc) {
		pc.reset();
		pc.emplace(line.a);
		GiveSession(*pc, 0x2345);
		pc->Process();
	} else {
		board.reset();
		echoQueue.clear();
		board.emplace(line.b);
		GiveSession(*board, 0x6B6B);
		board->Process();
	}

	PhaseStats second = RunPhase(line, *pc, *board, 100, 100, clockUs);
	std::snprintf(label, sizeof(label), "%s after restart", name);
	PrintPhase(label, second, rejoinRestarts);

	SIM_EXPECT(first.sendOk == 100);
	SIM_EXPECT(first.echoOk == 100);
	SIM_EXPECT(first.echoesInTwice == 0);
	// Every payload the board passed up is echoed once, and every echo reaches the PC.
	SIM_EXPECT(second.sendRetry == 0);
	SIM_EXPECT(second.echoRetry == 0);
	SIM_EXPECT(second.echoReset == 0);
	SIM_EXPECT(second.echoesInTwice == 0);
	SIM_EXPECT(second.echoesIn == second.deliveredOnce);
	SIM_EXPECT(second.echoOk == second.deliveredOnce);
	SIM_EXPECT(second.sendOk + second.sendReset == 100);
	if (restartPc) {
		// The PC restarted: every payload it sends after the restart is delivered and reported ok.
		SIM_EXPECT(second.sendReset == 0);
		SIM_EXPECT(second.deliveredOnce == second.sendOk);
	} else {
		// The board restarted. The PC's packets that the rebuilt board refuses are reset, up to a window (the page's rule for a receiver
		// that lost its session). So are the packets of the PC's new session still in flight when the board's own sync arrives, up to a
		// window: the board can have passed those up (Reliable.md, Limits). A payload reported ok was passed up.
		SIM_EXPECT(second.sendOk <= second.deliveredOnce);
		SIM_EXPECT(second.deliveredOnce <= second.sendOk + second.sendReset);
		SIM_EXPECT(second.sendReset <= 2 * 4);
	}

	SIM_EXPECT(second.lastUs - second.firstUs <= (first.lastUs - first.firstUs) * 6 / 5 + 5000);
}


// The same rebuild with packets in flight: the first 25 rounds of the phase run and stop, so payloads and echoes are waiting for their
// acknowledgements when the side is rebuilt. The side that stays up must give every packet it sent an outcome, once: ok or reset, never
// retryExhausted, and an ok echo (or payload) was received once. The rebuilt side's own packets have no outcome, as it lost them.
template<typename Line, typename Stack>
void InFlightScenario(const char* name, bool restartPc) {
	ResetRejoinState();
	Line line(VUB::Faults{});
	std::optional<Stack> pc;
	std::optional<Stack> board;
	pc.emplace(line.a);
	board.emplace(line.b);
	GiveSession(*pc, 0x1234);
	GiveSession(*board, 0x5A5A);
	uint32 clockUs = 0x0100;
	testNowUs = clockUs;
	line.SetNow(clockUs);
	pc->Process();
	board->Process();

	RunRounds(line, *pc, *board, 0, 100, clockUs, 25);
	uint16 payloadsInFlight = 0;
	uint16 echoesInFlight = 0;
	for (uint16 index = 0; index < RejoinInFlightIds; index++) {
		if (sendSent[index] && sendOutcomes[index] == 0) {
			payloadsInFlight++;
		}

		if (echoSent[index] && echoOutcomes[index] == 0) {
			echoesInFlight++;
		}
	}

	std::printf("%s, rebuilt at %u us: payloads in flight %u, echoes in flight %u\n", name, clockUs, payloadsInFlight, echoesInFlight);
	if (restartPc) {
		pc.reset();
		pc.emplace(line.a);
		GiveSession(*pc, 0x2345);
		pc->Process();
	} else {
		board.reset();
		echoQueue.clear();
		board.emplace(line.b);
		GiveSession(*board, 0x6B6B);
		board->Process();
	}

	PhaseStats second = RunPhase(line, *pc, *board, 100, 100, clockUs);
	DrainSurvivor(line, *pc, *board, restartPc, clockUs);
	char label[160];
	std::snprintf(label, sizeof(label), "%s, in flight, after restart", name);
	PrintPhase(label, SummarisePhase(100, 100, second.firstUs), rejoinRestarts);

	for (uint16 index = 0; index < RejoinInFlightIds; index++) {
		SIM_EXPECT(echoesIn[index] <= 1);
		SIM_EXPECT(deliveredAtBoard[index] <= 1);
		if (restartPc && echoSent[index]) {
			SIM_EXPECT(echoOutcomes[index] == 1);
			SIM_EXPECT(echoStatus[index] != ResultStatus::retryExhausted);
			if (echoOutcomes[index] == 1 && echoStatus[index] == ResultStatus::ok) {
				SIM_EXPECT(echoesIn[index] == 1);
			}
		}

		if (!restartPc && sendSent[index]) {
			SIM_EXPECT(sendOutcomes[index] == 1);
			SIM_EXPECT(sendStatus[index] != ResultStatus::retryExhausted);
			if (sendOutcomes[index] == 1 && sendStatus[index] == ResultStatus::ok) {
				SIM_EXPECT(deliveredAtBoard[index] == 1);
			}
		}
	}

	SIM_EXPECT(second.sendRetry == 0);
	SIM_EXPECT(second.echoRetry == 0);
	SIM_EXPECT(second.echoesInTwice == 0);
	SIM_EXPECT(second.sendOk + second.sendReset == 100);
	if (restartPc) {
		SIM_EXPECT(second.sendReset == 0);
		SIM_EXPECT(second.echoOk == 100);
	} else {
		SIM_EXPECT(second.sendReset <= 4);
	}
}


int main() {
	Scenario<VUB::PacketLine<64, 512>, RejoinPacket>("packet, PC restarts", true);
	Scenario<VUB::PacketLine<64, 512>, RejoinPacket>("packet, board restarts", false);
	Scenario<VUB::StreamLine<64, 512>, RejoinStream>("stream, PC restarts", true);
	Scenario<VUB::StreamLine<64, 512>, RejoinStream>("stream, board restarts", false);
	InFlightScenario<VUB::PacketLine<64, 512>, RejoinPacket>("packet, PC restarts", true);
	InFlightScenario<VUB::PacketLine<64, 512>, RejoinPacket>("packet, board restarts", false);
	InFlightScenario<VUB::StreamLine<64, 512>, RejoinStream>("stream, PC restarts", true);
	InFlightScenario<VUB::StreamLine<64, 512>, RejoinStream>("stream, board restarts", false);
	return SimFinish("TestVubReliableRejoin");
}
