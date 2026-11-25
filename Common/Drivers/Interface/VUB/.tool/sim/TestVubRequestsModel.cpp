#include "Chains.h"
#include "SimCheck.h"
#include <Drivers/Interface/VUB/Requests/Body.h>
#include <Drivers/Interface/VUB/Requests/Commands.h>
#include <Drivers/Interface/VUB/Requests/Handlers.h>
#include <Drivers/Interface/VUB/Requests.h>
#include <Drivers/Interface/VUB/TestKit/Random.h>
#include <Drivers/Interface/VUB/Transform/Crc.h>
#include <Utilities/Checksum/CRC/Preset/Crc16Modbus.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>


// The randomised model check of the requests layer. Two stacks, each a requester and a responder, run over a faulty packet line. The chains
// are listed below: with and without Reliable below the layer, in order and out of order, and with small and large tables. A run is fixed by
// its seed and its step count, so it repeats exactly.
//
// A step is one random action and one round of the line. The actions: sends of each command kind, pings (also with a padding above the
// maximum), events (plain, and Delivered with Reliable), replies held or sent at once, interim replies, stale ticket calls, cancels, bursts
// that fill the tables, link down and up, a restart of either side, and garbage packets fed to a stack. The clock starts a little below the
// 32-bit wrap, and jumps now and then.
//
// Every request carries an id in its body: the index of its record in the model. A ticket's handler receives the body, so the model knows the
// record of each request on the responder side. A requester's final handler is one function per lane, and a ping's handler is one function
// per pool entry, so the model knows the record of each request on the requester side.
//
// The oracle, checked at the event or at the end of the run:
//   - a request has exactly one end: a final, or the restart of its requester; a final handler runs at most once;
//   - a handler on the responder side runs at most once per request, except after a restart of the responder or on a duplicating line for
//     an unsequenced packet; a request the requester closed does not run after every copy of it could still be on the line;
//   - a final belongs to the request of its number and tag (its reply body names the record, and the ok status is done);
//   - interim replies come in rising order, none after the final, and with Reliable in order every interim sent is delivered before the ok
//     final;
//   - a responder sends at most one final per ticket; a ticket call after its end answers notAvailable;
//   - a cancel handler runs at most once per ticket, and never for a ticket that already had its final;
//   - a ping has one result, never operationCancelled; its round trip starts at a round of its side and lasts no longer than the time since
//     the ping was queued;
//   - a Delivered event runs at most once (more only after a restart of the responder), and an event whose handler never ran was reported
//     lost by its sender's counter;
//   - the counters: EventsLost lies between the events that never ran and the events the sender's stack accepted; Refused and LateReplies
//     are bounded by what the peer sent (the layer counts drops inside the layer, so the model sees only the bounds);
//   - at most as many tickets as the table holds are open on a responder, and at most as many requests as Open(n) on a requester;
//   - nothing stays open after the drain: every lane is free, every ticket is closed, every ping has its result, and the table takes its full
//     number again.
// The first violation stops the run and prints the chain, the seed and the step.
//
// Usage: TestVubRequestsModel [first seed, seed count, steps per run]. Without arguments it runs a few seeds of 600 steps on every chain.


// --- Bodies and statuses of the model -------------------------------------------------------------------------------------------------


// The request body. id is the record index of the request in the model; value is drawn from the seed.
struct ModelBody {
	uint32 id = 0;
	uint32 value = 0;
};


// The reply body of a call. The responder makes it, and the requester checks it against the record.
struct ModelReply {
	uint32 id = 0;
	uint32 value = 0;
};


namespace Model {

enum class Command : uint8 {
	call = 0x20,
	plain = 0x21,
	note = 0x22,
	kick = 0x23
};


enum class Event : uint8 {
	notice = 0x40,
	alarm = 0x41
};

}


// Interim statuses are 0, 1, 2 in the order the responder sends them. done is the status of every ok final.
enum class ModelStatus : uint8 {
	first = 0,
	second = 1,
	third = 2,
	done = 3
};


namespace VUB {


template<>
struct BodyOf<ModelBody> {
	static consteval auto Layout() {
		return Fields<ModelBody>()
			.Field<&ModelBody::id>()
			.Field<&ModelBody::value>();
	}
};


template<>
struct BodyOf<ModelReply> {
	static consteval auto Layout() {
		return Fields<ModelReply>()
			.Field<&ModelReply::id>()
			.Field<&ModelReply::value>();
	}
};


}


// --- The chains ------------------------------------------------------------------------------------------------------------------------


// Each chain is a set of constants: whether Reliable is below the requests layer, the in-order option of that Reliable, and the Open, Tickets
// and Refusals of the layer (window is the Reliable's window).
struct ChainCrc {
	static constexpr bool reliable = false;
	static constexpr bool inOrder = false;
	static constexpr std::size_t open = 6;
	static constexpr std::size_t tickets = 4;
	static constexpr std::size_t refusals = 4;
	static constexpr std::size_t window = 0;
	static constexpr const char* name = "crc";
};


struct ChainCrcSmall {
	static constexpr bool reliable = false;
	static constexpr bool inOrder = false;
	static constexpr std::size_t open = 1;
	static constexpr std::size_t tickets = 1;
	static constexpr std::size_t refusals = 0;
	static constexpr std::size_t window = 0;
	static constexpr const char* name = "crc-small";
};


struct ChainRel {
	static constexpr bool reliable = true;
	static constexpr bool inOrder = false;
	static constexpr std::size_t open = 6;
	static constexpr std::size_t tickets = 4;
	static constexpr std::size_t refusals = 4;
	static constexpr std::size_t window = 6;
	static constexpr const char* name = "reliable";
};


struct ChainRelInOrder {
	static constexpr bool reliable = true;
	static constexpr bool inOrder = true;
	static constexpr std::size_t open = 6;
	static constexpr std::size_t tickets = 4;
	static constexpr std::size_t refusals = 4;
	static constexpr std::size_t window = 6;
	static constexpr const char* name = "reliable-in-order";
};


struct ChainRelSmall {
	static constexpr bool reliable = true;
	static constexpr bool inOrder = false;
	static constexpr std::size_t open = 1;
	static constexpr std::size_t tickets = 1;
	static constexpr std::size_t refusals = 0;
	static constexpr std::size_t window = 1;
	static constexpr const char* name = "reliable-small";
};


struct ChainRelLarge {
	static constexpr bool reliable = true;
	static constexpr bool inOrder = false;
	static constexpr std::size_t open = 12;
	static constexpr std::size_t tickets = 12;
	static constexpr std::size_t refusals = 16;
	static constexpr std::size_t window = 12;
	static constexpr const char* name = "reliable-large";
};


// The most lanes any chain has. The lane after the last one is the spare lane: a send on it must be refused.
inline constexpr std::size_t ModelMaxLanes = 12;
inline constexpr std::size_t ModelMaxTickets = 12;
inline constexpr std::size_t ModelHeldSlots = 32;
inline constexpr std::size_t ModelPingSlots = 12;
inline constexpr std::size_t ModelRoundHistory = 1024;
// The resends and the timeout of Reliable in every chain that has it.
inline constexpr uint8 ModelRetries = 3;
inline constexpr uint16 ModelTimeoutMs = 200;
inline constexpr uint32 ModelTimeoutUs = ModelTimeoutMs * 1000u;


// --- The command sets: with Reliable (Note, and the Delivered event) and without it ----------------------------------------------------


// Command::call: a reply at once or later, interim replies, cancellable. Command::plain: a reply at once. Command::note: NoReply, which needs
// Reliable below. Command::kick: Fire, no reply and no ticket. Event::notice: a plain event. Event::alarm: a Delivered event, which needs Reliable.
using FullCommands = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<Model::Command::call>()
			.Request<ModelBody>()
			.Reply<ModelStatus, ModelReply>()
			.Interim()
			.Cancellable()
			.ReplyTimeoutMs(4000)
			.TicketTimeoutMs(6000)
		.Command<Model::Command::plain>()
			.Request<ModelBody>()
			.Reply<ModelStatus>()
			.ReplyTimeoutMs(3000)
			.TicketTimeoutMs(5000)
		.Command<Model::Command::note>()
			.Request<ModelBody>()
			.NoReply()
		.Command<Model::Command::kick>()
			.Request<ModelBody>()
			.Fire()
		.Event<Model::Event::notice>()
			.Body<ModelBody>()
		.Event<Model::Event::alarm>()
			.Body<ModelBody>()
			.Delivered();
}>;


// The same commands without Command::note and Event::alarm: NoReply and Delivered are compile errors without Reliable below.
using PathCommands = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<Model::Command::call>()
			.Request<ModelBody>()
			.Reply<ModelStatus, ModelReply>()
			.Interim()
			.Cancellable()
			.ReplyTimeoutMs(4000)
			.TicketTimeoutMs(6000)
		.Command<Model::Command::plain>()
			.Request<ModelBody>()
			.Reply<ModelStatus>()
			.ReplyTimeoutMs(3000)
			.TicketTimeoutMs(5000)
		.Command<Model::Command::kick>()
			.Request<ModelBody>()
			.Fire()
		.Event<Model::Event::notice>()
			.Body<ModelBody>();
}>;


template<typename Chain>
using CommandsOf = std::conditional_t<Chain::reliable, FullCommands, PathCommands>;


// --- Model state -----------------------------------------------------------------------------------------------------------------------


inline constexpr uint8 KindCall = 0;
inline constexpr uint8 KindPlain = 1;
inline constexpr uint8 KindNote = 2;
inline constexpr uint8 KindKick = 3;
inline constexpr uint8 KindNotice = 4;
inline constexpr uint8 KindAlarm = 5;

// The end kinds of the totals: a call, a plain request, a note, and a ping.
inline constexpr std::size_t EndCall = 0;
inline constexpr std::size_t EndPlain = 1;
inline constexpr std::size_t EndNote = 2;
inline constexpr std::size_t EndPing = 3;

// The end buckets of the totals.
inline constexpr std::size_t BucketOk = 0;
inline constexpr std::size_t BucketDeadline = 1;
inline constexpr std::size_t BucketCancel = 2;
inline constexpr std::size_t BucketRestart = 3;
inline constexpr std::size_t BucketOther = 4;


// What the model knows about one request or event, from the send to the end of the run.
struct ModelRecord {
	uint32 id = 0;
	uint8 kind = 0;
	uint8 sender = 0;
	uint16 number = 0;
	uint32 value = 0;
	uint32 instance = 0;
	bool accepted = false;
	bool acceptedDown = false;
	bool abandoned = false;
	bool finalSeen = false;
	bool responderFinal = false;
	bool responderRebuilt = false;
	bool reportedOk = false;
	uint32 served = 0;
	uint32 cancelRuns = 0;
	uint32 finals = 0;
	uint32 interimsSent = 0;
	uint32 interimsSeen = 0;
	int lastInterim = -1;
	uint32 closedUs = 0;
	uint32 replyValue = 0;
	// The reply values the responder made for the record, one for each run of its handler that finished a call. A copy that runs again can
	// make a second reply, and the requester may take either: the first ones are kept up to the size of the array.
	std::array<uint32, 8> made{};
	uint32 madeCount = 0;
};


// One open request of a requester: the record it belongs to and the number the layer gave it.
struct ModelLane {
	bool open = false;
	uint32 id = 0;
	uint16 number = 0;
	uint8 kind = 0;
};


// One ping of a requester: whether the layer holds it, and the time it was queued.
struct ModelPing {
	bool busy = false;
	uint32 queuedUs = 0;
};


// One ticket a responder holds for the peer: the ticket value, the record id, and the side that holds it.
template<typename Chain>
struct ModelHeld {
	VUB::Ticket<CommandsOf<Chain>, Model::Command::call> ticket{};
	uint32 id = 0;
	uint8 side = 0;
	uint8 interims = 0;
	bool used = false;
};


// The totals of one chain, printed after its runs.
struct ModelTotals {
	uint64 steps = 0;
	uint64 requests = 0;
	uint64 kicks = 0;
	uint64 notices = 0;
	uint64 alarms = 0;
	uint64 pings = 0;
	uint64 pingRefusals = 0;
	uint64 staleCalls = 0;
	uint64 ends[4][5] = {};
	uint64 dropped = 0;
	uint64 corrupted = 0;
	uint64 duplicated = 0;
	uint64 outaged = 0;
	uint64 garbage = 0;
	uint64 linkChanges = 0;
	uint64 rebuilds = 0;
	uint64 interimsSent = 0;
	uint64 finalsSent = 0;
};


inline std::vector<ModelRecord> modelRecords;
inline ModelTotals modelTotals;
inline uint32 modelSeed = 0;
inline uint32 modelStep = 0;
inline uint32 modelSteps = 600;
inline const char* modelLabel = "";
inline uint8 activeSide = 0;
inline bool modelViolated = false;
inline bool modelLinkUp = true;
inline bool modelHoldsClosed = false;
inline bool modelDuplicates = false;
inline bool modelReliableChain = false;
// The line of a run: keepsOrder keeps the order of the packets; reorders takes them apart by their delays. On a reordering line the model
// makes a reboot and a link change only when no packet of the old boot can still be on the line (see ModelCalm).
enum class ModelLineKind : uint8 {
	keepsOrder,
	reorders
};

inline ModelLineKind modelLineKind = ModelLineKind::keepsOrder;
inline uint32 modelLastSendUs = 0;
inline uint32 modelQuietUntilUs = 0;
inline uint32 modelNowUs = 0;
inline uint32 modelMaxDelayUs = 0;
inline uint32 modelRoundTimes[ModelRoundHistory] = {};
inline bool modelRoundLinkUp[ModelRoundHistory] = {};
inline std::size_t modelRoundCount = 0;
inline bool modelInOrder = false;
inline uint32 modelInstance[2] = {0, 0};
inline uint64 modelSentToward[2] = {0, 0};
inline uint64 modelRepliesToward[2] = {0, 0};
inline uint16 modelSessions[2] = {0x1234, 0x2345};
inline std::vector<bool> modelSessionUsed[2];
inline VUB::Random modelRandom(1);
inline ReportingPacketLine* modelLine = nullptr;
inline std::array<std::array<ModelLane, ModelMaxLanes + 1>, 2> modelLaneTable{};
inline std::array<std::array<ModelPing, ModelPingSlots>, 2> modelPingTable{};


template<typename Chain>
inline std::array<ModelHeld<Chain>, ModelHeldSlots> modelHeld{};


template<typename Chain>
struct StackOf;


// Records a violation of the oracle: the first one prints the run, the seed and the step, and stops the run.
inline void ModelCheck(bool condition, const char* text) {
	if (condition || modelViolated) {
		return;
	}

	modelViolated = true;
	std::printf("VIOLATION chain %s seed %u step %u: %s\n", modelLabel, static_cast<unsigned>(modelSeed), static_cast<unsigned>(modelStep), text);
	SimExpect(condition, text, __LINE__);
}


// Counts an end of a request or a ping: the kind, and the bucket of its result.
inline void ModelCountEnd(std::size_t endKind, ResultStatus result) {
	std::size_t bucket = BucketOther;
	if (result == ResultStatus::ok) {
		bucket = BucketOk;
	} else if (result == ResultStatus::timeout) {
		bucket = BucketDeadline;
	} else if (result == ResultStatus::operationCancelled) {
		bucket = BucketCancel;
	} else if (result == ResultStatus::reset) {
		bucket = BucketRestart;
	}

	modelTotals.ends[endKind][bucket]++;
}


// The end kind of a lane's request, from its command kind.
inline std::size_t ModelEndKindOf(uint8 kind) {
	if (kind == KindCall) {
		return EndCall;
	}

	if (kind == KindNote) {
		return EndNote;
	}

	return EndPlain;
}


// True when two request numbers of the layer are the same.
inline bool ModelSameNumber(VUB::RequestId first, VUB::RequestId second) {
	return first.number == second.number;
}


// True when no packet sent before closedUs is tried by its sender any more, and the time from which that holds. Such a packet had its first
// try by then, so ModelRetries + 1 timeouts remain at most, each one ending in a round with the link up; a link that comes back gives a
// try back.
inline bool ModelSettledSince(uint32 closedUs, uint32& settledUs) {
	std::size_t kept = modelRoundCount < ModelRoundHistory ? modelRoundCount : ModelRoundHistory;
	uint32 timeouts = ModelRetries + 1;
	uint32 lastUs = closedUs;
	bool wasUp = true;
	for (std::size_t age = kept; age > 0; age--) {
		std::size_t index = (modelRoundCount - age) % ModelRoundHistory;
		uint32 timeUs = modelRoundTimes[index];
		bool up = modelRoundLinkUp[index];
		if (static_cast<int32>(timeUs - closedUs) > 0) {
			if (up && !wasUp) {
				timeouts++;
				lastUs = timeUs;
			} else if (up && timeUs - lastUs >= ModelTimeoutUs) {
				lastUs = timeUs;
				timeouts--;
			}

			if (timeouts == 0) {
				settledUs = timeUs;
				return true;
			}
		}

		wasUp = up;
	}

	return false;
}


// True when a copy of a request, sent before its requester closed it, could no longer reach the responder in this round: the copy is due by
// the close plus the line's delay, and a run two rounds after the close is past that delay. An in-order receiver keeps a packet until the
// packets before it have arrived or were given up, so there the delay counts from the time they are settled.
inline bool ModelLateRun(const ModelRecord& record) {
	if (modelRoundCount < 3) {
		return false;
	}

	uint32 sinceUs = record.closedUs;
	if (modelInOrder && !ModelSettledSince(record.closedUs, sinceUs)) {
		return false;
	}

	uint32 twoRoundsBack = modelRoundTimes[(modelRoundCount - 3) % ModelRoundHistory];
	int elapsed = static_cast<int>(twoRoundsBack - sinceUs);
	return elapsed >= static_cast<int>(modelMaxDelayUs);
}


// True when a time is a round of the model: a ping's stamp is the clock of a round of its side.
inline bool ModelIsRoundTime(uint32 stamp) {
	std::size_t count = modelRoundCount < ModelRoundHistory ? modelRoundCount : ModelRoundHistory;
	for (std::size_t index = 0; index < count; index++) {
		if (modelRoundTimes[index] == stamp) {
			return true;
		}
	}

	return false;
}


// What the layer tells apart when a packet arrives twice (checked through the rules of ModelServe and the counters):
//   - without Reliable, a REQUEST whose number and tag match an open ticket of the responder is answered with that ticket (a copy does not
//     run again while the ticket is open);
//   - with Reliable, a sequenced packet is passed up once per session, and a copy behind the window is acknowledged and not passed up;
//   - an unsequenced packet (a Fire, an event that is not Delivered) carries no number, so each copy runs;
//   - a Delivered event and a NoReply are sequenced, so a copy does not run again unless a side was rebuilt.
// The record of a request that reached a responder, or nullptr when the id is not one the model sent. Also checks what a responder may do
// with the request: the right command, the right side, the right body, one run, and not after the requester closed it when the link came back.
inline ModelRecord* ModelServe(const ModelBody& body, uint8 kind) {
	if (body.id >= modelRecords.size()) {
		ModelCheck(false, "a request reached a responder with an id the model never sent");
		return nullptr;
	}

	ModelRecord& record = modelRecords[body.id];
	ModelCheck(record.accepted, "a request ran that the requester never got accepted");
	ModelCheck(record.kind == kind, "a request ran as a different command than the one it was sent as");
	ModelCheck(record.sender != activeSide, "a request ran on the side that sent it");
	ModelCheck(record.value == body.value, "a request ran with a body other than the one sent");
	record.served++;
	// An unsequenced packet (a Fire, a plain event) has no duplicate check below the layer on a duplicating line, so it may run once per copy.
	// A plain request on a path with no Reliable below may run again for a copy that comes after its ticket closed: the layer keeps no
	// record of a closed ticket, and a plain request closes at once. A call with a ticket is told apart while its ticket is open (see the
	// list in ModelTellsApart), so it may not run again. A responder that was rebuilt may run a request again: the design does not say how a
	// new session treats a copy of an old packet.
	bool unsequenced = kind == KindKick || kind == KindNotice;
	bool pathCopy = !modelReliableChain && (kind == KindPlain || kind == KindCall) && modelDuplicates;
	bool repeatAllowed = record.responderRebuilt || (unsequenced && modelDuplicates) || pathCopy;
	// A copy that runs again opens a ticket of its own, so the one final and the one cancel of the ticket start again.
	if (pathCopy && record.served >= 1) {
		record.responderFinal = false;
		record.cancelRuns = 0;
	}

	ModelCheck(record.served == 1 || repeatAllowed, "a handler ran twice for one request");
	ModelCheck(!(modelHoldsClosed && record.finalSeen && !record.acceptedDown && ModelLateRun(record)), "a request the requester closed ran after no copy of it could still be on the line");
	return &record;
}


// A free entry of the responder's held tickets, or nullptr. The count of open tickets of the side is checked against the table first.
template<typename Chain>
ModelHeld<Chain>* ModelFreeEntry() {
	std::size_t open = 0;
	ModelHeld<Chain>* free = nullptr;
	for (ModelHeld<Chain>& entry : modelHeld<Chain>) {
		if (entry.used && entry.ticket.Open()) {
			if (entry.side == activeSide) {
				open++;
			}

			continue;
		}

		// An entry whose ticket is closed is free: the layer closed it, and its record keeps the outcome.
		if (free == nullptr) {
			free = &entry;
		}
	}

	ModelCheck(open < Chain::tickets, "more tickets are open on a responder than its table holds");
	return open < Chain::tickets ? free : nullptr;
}


// The held entry whose ticket is open and has the request number of ticket, or nullptr.
template<typename Chain>
ModelHeld<Chain>* ModelFindOpen(const VUB::Ticket<CommandsOf<Chain>, Model::Command::call>& ticket) {
	for (ModelHeld<Chain>& entry : modelHeld<Chain>) {
		if (entry.used && entry.side == activeSide && entry.ticket.Open() && ModelSameNumber(entry.ticket.Number(), ticket.Number())) {
			return &entry;
		}
	}

	return nullptr;
}


// True when a held ticket of the request id is open on the responder.
template<typename Chain>
bool ModelOpenFor(uint32 id) {
	for (const ModelHeld<Chain>& entry : modelHeld<Chain>) {
		if (entry.used && entry.id == id && entry.ticket.Open()) {
			return true;
		}
	}

	return false;
}


// Finishes a held call with an ok final that carries a reply body the model makes for it. A closed ticket answers notAvailable.
template<typename Chain>
ResultStatus ModelFinishCall(ModelHeld<Chain>& entry) {
	ModelRecord& record = modelRecords[entry.id];
	bool wasOpen = entry.ticket.Open();
	uint32 made = modelRandom.Next();
	ModelReply reply;
	reply.id = entry.id;
	reply.value = made;
	ResultStatus result = entry.ticket.Finish(ModelStatus::done, reply);
	ModelCheck(wasOpen || result == ResultStatus::notAvailable, "a Finish on a closed ticket did not answer notAvailable");
	if (result == ResultStatus::ok) {
		ModelCheck(!record.responderFinal, "a second final left for one request");
		record.responderFinal = true;
		record.replyValue = made;
		if (record.madeCount < record.made.size()) {
			record.made[record.madeCount] = made;
		}

		record.madeCount++;
		modelTotals.finalsSent++;
		modelRepliesToward[record.sender]++;
	}

	return result;
}


// Sends an interim reply for a held call, with the next index as its status.
template<typename Chain>
void ModelInterimCall(ModelHeld<Chain>& entry) {
	bool wasOpen = entry.ticket.Open();
	ResultStatus result = entry.ticket.Interim(static_cast<ModelStatus>(entry.interims));
	ModelCheck(wasOpen || result == ResultStatus::notAvailable, "an interim on a closed ticket did not answer notAvailable");
	if (result == ResultStatus::ok) {
		entry.interims++;
		modelRecords[entry.id].interimsSent++;
		modelRepliesToward[modelRecords[entry.id].sender]++;
		modelTotals.interimsSent++;
	}
}


// Closes a held call with operationCancelled, from the responder's side. A closed ticket answers notAvailable.
template<typename Chain>
void ModelCancelTicket(ModelHeld<Chain>& entry) {
	bool wasOpen = entry.ticket.Open();
	ResultStatus result = entry.ticket.Cancelled();
	ModelCheck(wasOpen || result == ResultStatus::notAvailable, "a Cancelled on a closed ticket did not answer notAvailable");
	if (result == ResultStatus::ok) {
		ModelCheck(!modelRecords[entry.id].responderFinal, "a second final left for one request");
		modelRecords[entry.id].responderFinal = true;
		modelRepliesToward[modelRecords[entry.id].sender]++;
		modelTotals.finalsSent++;
	}
}


// A call on a ticket that has ended: each of its three calls must answer notAvailable.
template<typename Chain>
void ModelStaleCall(ModelHeld<Chain>& entry, uint32 action) {
	ModelCheck(!entry.ticket.Open(), "a ticket the model holds as ended is still open");
	ModelReply reply;
	reply.id = entry.id;
	ResultStatus result = ResultStatus::ok;
	if (action == 0) {
		result = entry.ticket.Finish(ModelStatus::done, reply);
	} else if (action == 1) {
		result = entry.ticket.Interim(ModelStatus::first);
	} else {
		result = entry.ticket.Cancelled();
	}

	ModelCheck(result == ResultStatus::notAvailable, "a ticket call after the end did not answer notAvailable");
	modelTotals.staleCalls++;
}


// --- Handlers of the responder ---------------------------------------------------------------------------------------------------------


// The ticket handler of a call. A third of the calls are answered at once, the rest are held for the model to finish later.
template<typename Chain>
void OnCall(VUB::Ticket<CommandsOf<Chain>, Model::Command::call> ticket, const ModelBody& body) {
	ModelRecord* record = ModelServe(body, KindCall);
	if (record == nullptr) {
		return;
	}

	// A copy of a call that arrives while the ticket of its first run is open is told apart by the layer: it is not run.
	if (record->served > 1 && ModelOpenFor<Chain>(body.id)) {
		ModelCheck(false, "a copy of a call ran while its ticket was open");
	}

	ModelHeld<Chain>* entry = ModelFreeEntry<Chain>();
	if (entry == nullptr) {
		return;
	}

	entry->ticket = ticket;
	entry->id = body.id;
	entry->side = activeSide;
	entry->interims = 0;
	entry->used = true;
	if (modelRandom.Chance(33)) {
		ModelFinishCall<Chain>(*entry);
	}
}


// The cancel handler of a call: it runs once, for a ticket that has not finished. Returning with the ticket open is the layer's own
// operationCancelled.
template<typename Chain>
void OnCancelCall(VUB::Ticket<CommandsOf<Chain>, Model::Command::call> ticket) {
	ModelHeld<Chain>* entry = ModelFindOpen<Chain>(ticket);
	ModelCheck(entry != nullptr, "a cancel handler ran for a ticket the model does not hold open");
	if (entry == nullptr) {
		return;
	}

	ModelRecord& record = modelRecords[entry->id];
	record.cancelRuns++;
	ModelCheck(record.cancelRuns == 1 || record.responderRebuilt, "a cancel handler ran twice for one request");
	ModelCheck(!record.responderFinal, "a cancel handler ran for a request that had already finished");
}


// The ticket handler of a plain request: answered at once.
template<typename Chain>
void OnPlain(VUB::Ticket<CommandsOf<Chain>, Model::Command::plain> ticket, const ModelBody& body) {
	ModelRecord* record = ModelServe(body, KindPlain);
	if (record == nullptr) {
		return;
	}

	ResultStatus result = ticket.Finish(ModelStatus::done);
	ModelCheck(result == ResultStatus::ok, "a plain request answered at once was not finished");
	if (result == ResultStatus::ok) {
		ModelCheck(!record->responderFinal, "a second final left for one request");
		record->responderFinal = true;
		modelRepliesToward[record->sender]++;
		modelTotals.finalsSent++;
	}
}


// The handler of a NoReply command: no ticket, runs at most once.
inline void OnNote(const ModelBody& body) {
	(void)ModelServe(body, KindNote);
}


// The handler of a Fire command: no ticket, runs at most once.
inline void OnKick(const ModelBody& body) {
	(void)ModelServe(body, KindKick);
}


// The handler of the plain event: runs once per copy on a duplicating line, otherwise at most once.
inline void OnNotice(const ModelBody& body) {
	(void)ModelServe(body, KindNotice);
}


// The handler of the Delivered event: at most once, Reliable removes the copies.
inline void OnAlarm(const ModelBody& body) {
	(void)ModelServe(body, KindAlarm);
}


// --- Handlers of the requester: one final and one interim per lane, one final per ping --------------------------------------------------


// Takes a lane's request out of the open state and counts its final. Returns the record, or nullptr for a lane the model had not opened.
template<typename Chain>
ModelRecord* ModelCloseLane(std::size_t side, std::size_t lane) {
	ModelLane& slot = modelLaneTable[side][lane];
	ModelCheck(slot.open, "a final reached a request slot the model had not opened");
	if (!slot.open) {
		return nullptr;
	}

	ModelRecord& record = modelRecords[slot.id];
	slot.open = false;
	record.finals++;
	ModelCheck(record.finals == 1, "a request got a second final");
	record.finalSeen = true;
	record.closedUs = modelNowUs;
	return &record;
}


template<typename Chain, std::size_t Side, std::size_t Lane>
void CallFinal(const VUB::Final<ModelStatus, ModelReply>& reply) {
	ModelRecord* record = ModelCloseLane<Chain>(Side, Lane);
	if (record == nullptr) {
		return;
	}

	ModelCountEnd(EndCall, reply.result);
	if (reply.result != ResultStatus::ok) {
		return;
	}

	ModelCheck(record->served >= 1, "a request was answered ok before its handler ran");
	ModelCheck(reply.status == ModelStatus::done, "an ok final carries a status other than done");
	bool made = record->madeCount > record->made.size();
	for (std::size_t index = 0; index < record->madeCount && index < record->made.size(); index++) {
		made = made || record->made[index] == reply.body.value;
	}

	ModelCheck(reply.body.id == record->id && made, "an ok final has a reply body other than the ones the responder made");
	ModelCheck(record->interimsSeen <= record->interimsSent, "more interim replies arrived than were sent");
	if constexpr (Chain::inOrder) {
		ModelCheck(record->interimsSeen == record->interimsSent, "an ok final arrived before its interim replies, on an in-order link");
	}
}


template<typename Chain, std::size_t Side, std::size_t Lane>
void CallInterim(const VUB::Progress<ModelStatus>& progress) {
	const ModelLane& slot = modelLaneTable[Side][Lane];
	ModelCheck(slot.open, "an interim reply reached a request that is not open");
	if (!slot.open) {
		return;
	}

	ModelRecord& record = modelRecords[slot.id];
	ModelCheck(record.finals == 0, "an interim reply came after the final");
	ModelCheck(progress.result == ResultStatus::ok, "an interim reply of a call was not ok");
	int status = static_cast<int>(progress.status);
	ModelCheck(status > record.lastInterim, "interim replies are not in rising order");
	record.lastInterim = status;
	record.interimsSeen++;
}


template<typename Chain, std::size_t Side, std::size_t Lane>
void PlainFinal(const VUB::Final<ModelStatus, VUB::NoBody>& reply) {
	ModelRecord* record = ModelCloseLane<Chain>(Side, Lane);
	if (record == nullptr) {
		return;
	}

	ModelCountEnd(EndPlain, reply.result);
	if (reply.result != ResultStatus::ok) {
		return;
	}

	ModelCheck(record->served >= 1, "a plain request was answered ok before its handler ran");
	ModelCheck(reply.status == ModelStatus::done, "an ok final of a plain request carries a status other than done");
}


template<typename Chain, std::size_t Side, std::size_t Lane>
void NoteFinal(ResultStatus result) {
	ModelRecord* record = ModelCloseLane<Chain>(Side, Lane);
	if (record == nullptr) {
		return;
	}

	ModelCountEnd(EndNote, result);
	if (result == ResultStatus::ok) {
		// The outcome ok is the delivery to the peer's Reliable; an in-order receiver may hand the packet up later, so the handler is checked at the end.
		record->reportedOk = true;
	}
}


// The handler of a ping of one pool entry: one result per accepted ping, never operationCancelled, and a round trip that starts at a round of
// the side and lasts no longer than the time since the ping was queued.
template<typename Chain, std::size_t Side, std::size_t Index>
void PingDone(ResultStatus result, uint32 roundTrip) {
	ModelPing& ping = modelPingTable[Side][Index];
	ModelCheck(ping.busy, "a ping handler ran for a ping the model does not hold");
	ModelCheck(result != ResultStatus::operationCancelled, "a ping was reported operationCancelled");
	ModelCountEnd(EndPing, result);
	if (result == ResultStatus::ok) {
		uint32 stamp = modelNowUs - roundTrip;
		ModelCheck(ModelIsRoundTime(stamp), "a ping's round trip does not start at a round of its side");
		ModelCheck(roundTrip <= modelNowUs - ping.queuedUs, "a ping's round trip is longer than the time since it was queued");
	}

	ping.busy = false;
}


// --- Handler lists ---------------------------------------------------------------------------------------------------------------------


template<typename Chain>
using FullHandlersOf = VUB::Handlers<
	VUB::On<Model::Command::call, &OnCall<Chain>>,
	VUB::OnCancel<Model::Command::call, &OnCancelCall<Chain>>,
	VUB::On<Model::Command::plain, &OnPlain<Chain>>,
	VUB::On<Model::Command::note, &OnNote>,
	VUB::On<Model::Command::kick, &OnKick>,
	VUB::OnEvent<Model::Event::notice, &OnNotice>,
	VUB::OnEvent<Model::Event::alarm, &OnAlarm>>;


template<typename Chain>
using PathHandlersOf = VUB::Handlers<
	VUB::On<Model::Command::call, &OnCall<Chain>>,
	VUB::OnCancel<Model::Command::call, &OnCancelCall<Chain>>,
	VUB::On<Model::Command::plain, &OnPlain<Chain>>,
	VUB::On<Model::Command::kick, &OnKick>,
	VUB::OnEvent<Model::Event::notice, &OnNotice>>;


template<typename Chain>
using HandlersOf = std::conditional_t<Chain::reliable, FullHandlersOf<Chain>, PathHandlersOf<Chain>>;


// --- Layer part: the stacks of the chains ----------------------------------------------------------------------------------------------


// Each chain's stack: the requests layer with the Counted counters, the ping on both sides, the CRC and the packet line. With Reliable, the
// Reliable layer is below the requests layer and above the CRC.
using StackCrcT = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<PathCommands, PathHandlersOf<ChainCrc>>>().Open(6).Tickets(4).Refusals(4).Counted().Ping(16).AnswerPing(16)
		.Transform<VUB::Crc<Checksum::Crc16Modbus>>()
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using StackCrcSmallT = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<PathCommands, PathHandlersOf<ChainCrcSmall>>>().Open(1).Tickets(1).Refusals(0).Counted().Ping(16).AnswerPing(16)
		.Transform<VUB::Crc<Checksum::Crc16Modbus>>()
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using StackRelT = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<FullCommands, FullHandlersOf<ChainRel>>>().Open(6).Tickets(4).Refusals(4).Counted().Ping(16).AnswerPing(16)
		.Layer<VUB::ReliableOf<40, 6>>().Retries(ModelRetries).TimeoutMs(ModelTimeoutMs).Window(6)
		.Transform<VUB::Crc<Checksum::Crc16Modbus>>()
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using StackRelInOrderT = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<FullCommands, FullHandlersOf<ChainRelInOrder>>>().Open(6).Tickets(4).Refusals(4).Counted().Ping(16).AnswerPing(16)
		.Layer<VUB::ReliableOf<40, 6, true>>().Retries(ModelRetries).TimeoutMs(ModelTimeoutMs).Window(6)
		.Transform<VUB::Crc<Checksum::Crc16Modbus>>()
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using StackRelSmallT = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<FullCommands, FullHandlersOf<ChainRelSmall>>>().Open(1).Tickets(1).Refusals(0).Counted().Ping(16).AnswerPing(16)
		.Layer<VUB::ReliableOf<40, 1>>().Retries(ModelRetries).TimeoutMs(ModelTimeoutMs).Window(1)
		.Transform<VUB::Crc<Checksum::Crc16Modbus>>()
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using StackRelLargeT = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<FullCommands, FullHandlersOf<ChainRelLarge>>>().Open(12).Tickets(12).Refusals(16).Counted().Ping(16).AnswerPing(16)
		.Layer<VUB::ReliableOf<40, 12>>().Retries(ModelRetries).TimeoutMs(ModelTimeoutMs).Window(12)
		.Transform<VUB::Crc<Checksum::Crc16Modbus>>()
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


template<>
struct StackOf<ChainCrc> {
	using Type = StackCrcT;
};


template<>
struct StackOf<ChainCrcSmall> {
	using Type = StackCrcSmallT;
};


template<>
struct StackOf<ChainRel> {
	using Type = StackRelT;
};


template<>
struct StackOf<ChainRelInOrder> {
	using Type = StackRelInOrderT;
};


template<>
struct StackOf<ChainRelSmall> {
	using Type = StackRelSmallT;
};


template<>
struct StackOf<ChainRelLarge> {
	using Type = StackRelLargeT;
};


template<typename Chain>
using HandleOf = decltype(std::declval<typename StackOf<Chain>::Type&>().Requests());


template<typename Chain>
inline std::optional<typename StackOf<Chain>::Type> modelStacks[2];


// The Reliable layer of a chain: the layer's type, which the stack finds to give its session.
template<typename Chain>
using ReliableOfChain = std::conditional_t<Chain::inOrder,
	VUB::ReliableOf<40, Chain::window, true>,
	VUB::ReliableOf<40, Chain::window>>;


// The number of a request as the layer gives it: the handle's Send returns it in a Result.
inline ResultStatus ModelNumberOf(Result<VUB::RequestId>& sent, uint16& number) {
	if (sent.IsErr()) {
		return sent.Error();
	}

	number = sent.Value().number;
	return ResultStatus::ok;
}


// Sends a request of one kind on one lane of one side. The send is the one the layer makes; the lane is only the handler it names.
template<typename Chain, std::size_t Side, std::size_t Lane>
ResultStatus ModelSendLane(uint8 kind, const ModelBody& body, uint16& number) {
	HandleOf<Chain> handle = modelStacks<Chain>[Side]->Requests();
	if (kind == KindCall) {
		Result<VUB::RequestId> sent = handle.template Send<Model::Command::call, &CallFinal<Chain, Side, Lane>, &CallInterim<Chain, Side, Lane>>(body);
		return ModelNumberOf(sent, number);
	}

	if (kind == KindPlain) {
		Result<VUB::RequestId> sent = handle.template Send<Model::Command::plain, &PlainFinal<Chain, Side, Lane>>(body);
		return ModelNumberOf(sent, number);
	}

	if constexpr (Chain::reliable) {
		if (kind == KindNote) {
			Result<VUB::RequestId> sent = handle.template Send<Model::Command::note, &NoteFinal<Chain, Side, Lane>>(body);
			return ModelNumberOf(sent, number);
		}
	}

	return ResultStatus::invalidParameter;
}


template<typename Chain, std::size_t Side, std::size_t Lane = 0>
ResultStatus ModelSendOnLane(std::size_t lane, uint8 kind, const ModelBody& body, uint16& number) {
	if constexpr (Lane <= Chain::open) {
		if (lane == Lane) {
			return ModelSendLane<Chain, Side, Lane>(kind, body, number);
		}

		return ModelSendOnLane<Chain, Side, Lane + 1>(lane, kind, body, number);
	} else {
		return ResultStatus::invalidParameter;
	}
}


template<typename Chain, std::size_t Side = 0>
ResultStatus ModelSendOnSide(std::size_t side, std::size_t lane, uint8 kind, const ModelBody& body, uint16& number) {
	if constexpr (Side < 2) {
		if (side == Side) {
			return ModelSendOnLane<Chain, Side>(lane, kind, body, number);
		}

		return ModelSendOnSide<Chain, Side + 1>(side, lane, kind, body, number);
	} else {
		return ResultStatus::invalidParameter;
	}
}


// A ping of one pool entry: the handler is the one of that entry.
template<typename Chain, std::size_t Side, std::size_t Index = 0>
ResultStatus ModelPingAt(std::size_t index, uint16 padding) {
	if constexpr (Index < ModelPingSlots) {
		if (index == Index) {
			HandleOf<Chain> handle = modelStacks<Chain>[Side]->Requests();
			return handle.template Ping<&PingDone<Chain, Side, Index>>(padding);
		}

		return ModelPingAt<Chain, Side, Index + 1>(index, padding);
	} else {
		return ResultStatus::invalidParameter;
	}
}


template<typename Chain, std::size_t Side = 0>
ResultStatus ModelPingOnSide(std::size_t side, std::size_t index, uint16 padding) {
	if constexpr (Side < 2) {
		if (side == Side) {
			return ModelPingAt<Chain, Side>(index, padding);
		}

		return ModelPingOnSide<Chain, Side + 1>(side, index, padding);
	} else {
		return ResultStatus::invalidParameter;
	}
}


// Emits an event of one kind from a side: the plain notice, or the alarm that is Delivered.
template<typename Chain>
ResultStatus ModelEmit(std::size_t side, uint8 kind, const ModelBody& body) {
	HandleOf<Chain> handle = modelStacks<Chain>[side]->Requests();
	if (kind == KindNotice) {
		return handle.template Emit<Model::Event::notice>(body);
	}

	if constexpr (Chain::reliable) {
		return handle.template Emit<Model::Event::alarm>(body);
	} else {
		return ResultStatus::invalidParameter;
	}
}


template<typename Chain>
std::size_t ModelFreeLane(std::size_t side) {
	for (std::size_t lane = 0; lane < Chain::open; lane++) {
		if (!modelLaneTable[side][lane].open) {
			return lane;
		}
	}

	return Chain::open;
}


template<typename Chain>
std::size_t ModelOpenLanes(std::size_t side) {
	std::size_t open = 0;
	for (std::size_t lane = 0; lane < Chain::open; lane++) {
		if (modelLaneTable[side][lane].open) {
			open++;
		}
	}

	return open;
}


template<typename Chain>
std::size_t ModelFreePing(std::size_t side) {
	for (std::size_t index = 0; index < Chain::open; index++) {
		if (!modelPingTable[side][index].busy) {
			return index;
		}
	}

	return Chain::open;
}


// Issues one request, event or fire of a kind from a side. A kind with a lane takes the first free lane; with none, the send goes on the spare
// lane and must be refused. The record is made first, so the body carries its id.
template<typename Chain>
void ModelIssue(std::size_t side, uint8 kind) {
	std::size_t lane = ModelFreeLane<Chain>(side);
	ModelRecord record;
	record.id = static_cast<uint32>(modelRecords.size());
	record.kind = kind;
	record.sender = static_cast<uint8>(side);
	record.value = modelRandom.Next();
	record.instance = modelInstance[side];
	uint32 id = record.id;
	modelRecords.push_back(record);

	ModelBody body;
	body.id = id;
	body.value = record.value;
	uint16 number = 0;
	ResultStatus result = ResultStatus::busy;

	if (kind == KindKick) {
		HandleOf<Chain> handle = modelStacks<Chain>[side]->Requests();
		result = handle.template Fire<Model::Command::kick>(body);
	} else if (kind == KindNotice || kind == KindAlarm) {
		result = ModelEmit<Chain>(side, kind, body);
	} else if (lane < Chain::open) {
		result = ModelSendOnSide<Chain>(side, lane, kind, body, number);
	} else {
		result = ModelSendOnSide<Chain>(side, Chain::open, kind, body, number);
		ModelCheck(result != ResultStatus::ok, "a request was accepted with every slot in use");
		return;
	}

	if (result != ResultStatus::ok) {
		return;
	}

	ModelRecord& stored = modelRecords[id];
	stored.accepted = true;
	stored.acceptedDown = !modelLinkUp;
	stored.number = number;
	if (kind == KindKick) {
		modelTotals.kicks++;
	} else if (kind == KindNotice) {
		modelTotals.notices++;
	} else if (kind == KindAlarm) {
		modelTotals.alarms++;
	} else {
		modelTotals.requests++;
		modelSentToward[1 - side]++;
		ModelLane& slot = modelLaneTable[side][lane];
		slot.open = true;
		slot.id = id;
		slot.number = number;
		slot.kind = kind;
	}
}


// A ping from a side with the padding. A padding above the layer's maximum is refused with messageTooLong and takes no slot.
template<typename Chain>
void ModelIssuePing(std::size_t side, uint16 padding) {
	std::size_t index = ModelFreePing<Chain>(side);
	if (index >= Chain::open) {
		return;
	}

	ResultStatus result = ModelPingOnSide<Chain>(side, index, padding);
	if (padding > 16) {
		ModelCheck(result == ResultStatus::messageTooLong, "a ping with a padding above the maximum was not refused with messageTooLong");
		modelTotals.pingRefusals++;
		return;
	}

	if (result != ResultStatus::ok) {
		return;
	}

	ModelPing& ping = modelPingTable[side][index];
	ping.busy = true;
	ping.queuedUs = modelNowUs;
	modelTotals.pings++;
	modelSentToward[1 - side]++;
}


// Cancels the open call on one lane of a side.
template<typename Chain>
void ModelCancel(std::size_t side, std::size_t lane) {
	ModelLane& slot = modelLaneTable[side][lane];
	HandleOf<Chain> handle = modelStacks<Chain>[side]->Requests();
	VUB::RequestId id{};
	id.number = slot.number;
	(void)handle.template Cancel<Model::Command::call>(id);
}


// Gives a stack its session value. With Reliable below, the Reliable layer of the stack gets it. Without Reliable, the requests handle gets it.
template<typename Chain, typename StackT>
void ModelGiveSession(StackT& stack, uint16 value) {
	if constexpr (Chain::reliable) {
		stack.template Find<ReliableOfChain<Chain>>().SetSession(value);
	} else {
		HandleOf<Chain> handle = stack.Requests();
		handle.SetSession(value);
	}
}


// True when two sessions may be given to the two sides: equal values are not allowed, since the requests of the two boots would share a number.
inline bool ModelSessionsAllowed(uint16 first, uint16 second) {
	return first != second;
}


// The session of a side's next boot. Each boot of a side gets a value no earlier boot of that side had, and the value is next to the other
// side's value when it can be. The layer gives different boots different first numbers only for values that follow each other, so a
// value is kept next to the other side's whenever it is free.
inline uint16 ModelNextSession(std::size_t side) {
	uint16 other = modelSessions[1 - side];
	std::vector<bool>& used = modelSessionUsed[side];
	uint16 candidates[2] = {static_cast<uint16>(other + 1), static_cast<uint16>(other - 1)};
	for (uint16 candidate : candidates) {
		if (candidate != other && !used[candidate]) {
			used[candidate] = true;
			return candidate;
		}
	}

	for (uint32 offset = 2; offset < 65536; offset++) {
		uint16 candidate = static_cast<uint16>(other + offset);
		if (candidate != other && candidate != 0 && !used[candidate]) {
			used[candidate] = true;
			return candidate;
		}
	}

	ModelCheck(false, "no session value is left for a boot");
	return other;
}


// Restarts one side: its open lanes and its pings are gone with the old stack, and its tickets are dropped without their cancel handlers. The
// stack is built again with a session value of its own.
template<typename Chain>
void ModelRebuild(std::size_t side) {
	modelTotals.rebuilds++;
	for (std::size_t lane = 0; lane < Chain::open; lane++) {
		ModelLane& slot = modelLaneTable[side][lane];
		if (slot.open) {
			modelRecords[slot.id].abandoned = true;
			ModelCountEnd(ModelEndKindOf(slot.kind), ResultStatus::reset);
			slot.open = false;
		}
	}

	for (std::size_t index = 0; index < Chain::open; index++) {
		ModelPing& ping = modelPingTable[side][index];
		if (ping.busy) {
			ModelCountEnd(EndPing, ResultStatus::reset);
			ping.busy = false;
		}
	}

	// Every request the rebuilt side could have run is marked: its handler may run again on the new stack, and its cancel check is skipped.
	for (ModelRecord& record : modelRecords) {
		if (record.sender != side) {
			record.responderRebuilt = true;
		}
	}

	for (ModelHeld<Chain>& entry : modelHeld<Chain>) {
		if (entry.used && entry.side == side) {
			entry.used = false;
		}
	}

	modelStacks<Chain>[side].reset();
	modelSessions[side] = ModelNextSession(side);
	modelStacks<Chain>[side].emplace(side == 0 ? modelLine->a : modelLine->b);
	ModelGiveSession<Chain>(*modelStacks<Chain>[side], modelSessions[side]);
	modelInstance[side]++;
	// A new boot waits one delay before its first send, on a reordering line.
	if (modelLineKind == ModelLineKind::reorders) {
		modelQuietUntilUs = modelNowUs + modelMaxDelayUs + 10000;
	}
}


// Drops the held entries whose tickets the layer has closed.
template<typename Chain>
void ModelSweep() {
	for (ModelHeld<Chain>& entry : modelHeld<Chain>) {
		if (entry.used && !entry.ticket.Open()) {
			entry.used = false;
		}
	}
}


template<typename Chain>
void ModelProcess(std::size_t side) {
	activeSide = static_cast<uint8>(side);
	modelStacks<Chain>[side]->Process();
}


// Hands the packets of one direction to the stack on side.
template<typename Chain>
void ModelDeliver(std::size_t side) {
	if (side == 1) {
		modelLine->DeliverToB(*modelStacks<Chain>[1]);
	} else {
		modelLine->DeliverToA(*modelStacks<Chain>[0]);
	}
}


// One round: side 0 runs, its packets reach side 1, side 1 runs, its packets reach side 0, side 0 runs.
template<typename Chain>
void ModelRound(uint32 advanceUs) {
	modelNowUs += advanceUs;
	testNowUs = modelNowUs;
	modelRoundTimes[modelRoundCount % ModelRoundHistory] = modelNowUs;
	modelRoundLinkUp[modelRoundCount % ModelRoundHistory] = modelLinkUp;
	modelRoundCount++;
	modelLine->SetNow(modelNowUs);
	ModelProcess<Chain>(0);
	ModelDeliver<Chain>(1);
	ModelProcess<Chain>(1);
	ModelDeliver<Chain>(0);
	ModelProcess<Chain>(0);
}


inline void ModelSetLink(bool up) {
	if (up == modelLinkUp) {
		return;
	}

	modelLinkUp = up;
	modelLine->SetLinkUp(up);
	modelTotals.linkChanges++;
	if (modelLineKind == ModelLineKind::reorders) {
		modelQuietUntilUs = modelNowUs + modelMaxDelayUs + 10000;
	}
}


// One random action of a step. The rolls are drawn from the seed, so a run repeats exactly.
// True when the model may make a reboot or a link change, or send: on a reordering line, no packet of the run may stay on the line longer than
// the longest delay, and a reboot waits one such delay ahead of its first send. So a reboot needs the line to be quiet for the delay and the
// link up; a link change down needs the same quiet time; after a reboot or a link change the sends wait the delay again. A keepsOrder
// line has no such condition.
inline bool ModelCalm() {
	if (modelLineKind == ModelLineKind::keepsOrder) {
		return true;
	}

	return modelLinkUp && static_cast<int32>(modelNowUs - modelQuietUntilUs) >= 0;
}


inline bool ModelQuietForDelay() {
	return static_cast<int32>(modelNowUs - modelLastSendUs) >= static_cast<int32>(modelMaxDelayUs) + 10000;
}


template<typename Chain>
void ModelAct() {
	uint32 roll = modelRandom.Below(100);
	std::size_t side = modelRandom.Below(2);
	bool calm = ModelCalm();

	// Sends (rolls below 70) wait while the calm condition fails. A link change down (rolls 70 to 75) and a reboot (rolls 76 and 77) need the
	// line quiet for a delay as well; a link change up is always allowed.
	if (roll < 70 && !calm) {
		return;
	}

	bool reorders = modelLineKind == ModelLineKind::reorders;
	if (reorders && roll >= 70 && roll < 76 && modelLinkUp && !(calm && ModelQuietForDelay())) {
		return;
	}

	if (reorders && roll >= 76 && roll < 78 && !(calm && ModelQuietForDelay())) {
		return;
	}

	if (roll < 70) {
		modelLastSendUs = modelNowUs;
	}

	if (roll < 35) {
		uint32 pick = modelRandom.Below(100);
		if (pick < 10) {
			uint16 padding = static_cast<uint16>(modelRandom.Below(20));
			ModelIssuePing<Chain>(side, padding);
			return;
		}

		uint8 kind = KindKick;
		if constexpr (Chain::reliable) {
			if (pick < 40) {
				kind = KindCall;
			} else if (pick < 55) {
				kind = KindPlain;
			} else if (pick < 65) {
				kind = KindNote;
			} else if (pick < 70) {
				kind = KindKick;
			} else if (pick < 85) {
				kind = KindNotice;
			} else {
				kind = KindAlarm;
			}
		} else {
			if (pick < 45) {
				kind = KindCall;
			} else if (pick < 65) {
				kind = KindPlain;
			} else if (pick < 70) {
				kind = KindKick;
			} else {
				kind = KindNotice;
			}
		}

		ModelIssue<Chain>(side, kind);
	} else if (roll < 50) {
		ModelHeld<Chain>& entry = modelHeld<Chain>[modelRandom.Below(ModelHeldSlots)];
		if (entry.used && entry.ticket.Open()) {
			uint32 action = modelRandom.Below(10);
			if (action < 6) {
				ModelFinishCall<Chain>(entry);
			} else if (action < 8) {
				if (entry.interims < 3) {
					ModelInterimCall<Chain>(entry);
				}
			} else if (action < 9) {
				ModelCancelTicket<Chain>(entry);
			}
		} else if (entry.used) {
			ModelStaleCall<Chain>(entry, modelRandom.Below(3));
		}
	} else if (roll < 60) {
		std::size_t start = modelRandom.Below(Chain::open);
		for (std::size_t offset = 0; offset < Chain::open; offset++) {
			std::size_t lane = (start + offset) % Chain::open;
			const ModelLane& slot = modelLaneTable[side][lane];
			if (slot.open && slot.kind == KindCall) {
				ModelCancel<Chain>(side, lane);
				break;
			}
		}
	} else if (roll < 70) {
		for (uint32 index = 0; index < 8; index++) {
			ModelIssue<Chain>(side, index < 5 ? KindCall : KindPlain);
		}
	} else if (roll < 76) {
		ModelSetLink(!modelLinkUp);
	} else if (roll < 78) {
		ModelRebuild<Chain>(side);
	} else if (roll < 80) {
		// A garbage packet, fed to a stack as if the line had carried it. It is random bytes of a random length.
		std::array<uint8, 45> junk{};
		std::size_t length = 1 + modelRandom.Below(static_cast<uint32>(junk.size()));
		for (std::size_t index = 0; index < length; index++) {
			junk[index] = static_cast<uint8>(modelRandom.Next());
		}

		activeSide = static_cast<uint8>(side);
		modelStacks<Chain>[side]->FeedPacket(std::span<const uint8>(junk.data(), length));
		modelTotals.garbage++;
	}
}


// Opens the table of a side. The plain requests go out in bursts with no round between the sends of one burst. A refused send is retried
// after a round. The check is that the layer takes the full number again.
template<typename Chain>
void ModelFillTables(std::size_t side) {
	for (uint32 attempt = 0; attempt < 3000 && ModelOpenLanes<Chain>(side) < Chain::open && !modelViolated; attempt++) {
		for (uint32 tries = 0; tries < Chain::open && ModelOpenLanes<Chain>(side) < Chain::open; tries++) {
			ModelIssue<Chain>(side, KindPlain);
		}

		if (ModelOpenLanes<Chain>(side) < Chain::open) {
			ModelRound<Chain>(10000);
		}
	}

	ModelCheck(ModelOpenLanes<Chain>(side) == Chain::open, "the requests table did not take its full number again");
}


// The checks at the end of a run, after the drain. Every deadline has passed by then.
template<typename Chain>
void ModelFinalChecks() {
	for (std::size_t side = 0; side < 2; side++) {
		ModelCheck(ModelOpenLanes<Chain>(side) == 0, "a request is still open after the drain");
		for (std::size_t index = 0; index < Chain::open; index++) {
			ModelCheck(!modelPingTable[side][index].busy, "a ping has no result after the drain");
		}
	}

	for (ModelHeld<Chain>& entry : modelHeld<Chain>) {
		ModelCheck(!(entry.used && entry.ticket.Open()), "a ticket is still open after the drain");
	}

	for (const ModelRecord& record : modelRecords) {
		if (!record.accepted || record.kind == KindKick) {
			continue;
		}

		if (record.kind == KindNotice) {
			ModelCheck(record.served <= 1 || modelDuplicates || record.responderRebuilt, "a plain event ran twice on a line that does not duplicate");
			continue;
		}

		if (record.kind == KindAlarm) {
			ModelCheck(record.served <= 1 || record.responderRebuilt, "a Delivered event ran twice");
			continue;
		}

		if (record.abandoned) {
			ModelCheck(record.finals == 0, "a request its requester lost in a restart got a final");
			continue;
		}

		ModelCheck(record.finals == 1, "an accepted request got no final, or two");
		if (record.reportedOk) {
			ModelCheck(record.served >= 1, "a NoReply request reported ok did not run");
		}
	}

	// The counters: EventsLost lies between the Delivered events of a side that never ran and the ones its stack accepted; Refused and
	// LateReplies are bounded by what the peer sent over the run.
	for (std::size_t side = 0; side < 2; side++) {
		uint32 lowerLost = 0;
		uint32 acceptedLost = 0;
		for (const ModelRecord& record : modelRecords) {
			if (record.kind == KindAlarm && record.accepted && record.sender == side && record.instance == modelInstance[side]) {
				acceptedLost++;
				if (record.served == 0) {
					lowerLost++;
				}
			}
		}

		HandleOf<Chain> handle = modelStacks<Chain>[side]->Requests();
		uint16 lost = handle.EventsLost();
		ModelCheck(lost >= lowerLost, "EventsLost is below the Delivered events that never ran");
		ModelCheck(lost <= acceptedLost, "EventsLost is above the Delivered events the stack accepted");
		ModelCheck(static_cast<uint64>(handle.Refused()) <= 2 * modelSentToward[side], "Refused is above what the peer sent");
		ModelCheck(static_cast<uint64>(handle.LateReplies()) <= 2 * modelRepliesToward[side], "LateReplies is above what the peer replied");
	}
}


template<typename Chain>
void ModelDrain(uint32 rounds) {
	for (uint32 round = 0; round < rounds && !modelViolated; round++) {
		ModelSweep<Chain>();
		ModelRound<Chain>(10000);
	}
}


// The wire counts of a line, added to the totals: the faults the line injected.
inline void ModelCountFaults() {
	VUB::WireCounts first = modelLine->toB.Counts();
	VUB::WireCounts second = modelLine->toA.Counts();
	modelTotals.dropped += first.dropped + second.dropped;
	modelTotals.corrupted += first.corrupted + second.corrupted;
	modelTotals.duplicated += first.duplicated + second.duplicated;
	modelTotals.outaged += first.outaged + second.outaged;
}


// One run of the model with one seed on one chain: the faults, the steps, the drain, the table check and the final checks.
template<typename Chain>
void ModelRunSeed(uint32 seed, ModelLineKind kind) {
	modelLineKind = kind;
	modelLastSendUs = 0;
	modelQuietUntilUs = 0;
	modelReliableChain = Chain::reliable;
	modelSeed = seed;
	modelStep = 0;
	modelViolated = false;
	modelRandom = VUB::Random(seed);
	modelRecords.clear();
	modelLinkUp = true;
	modelNowUs = 0xFFFF0000u;
	modelRoundCount = 0;
	modelInstance[0] = 0;
	modelInstance[1] = 0;
	modelSentToward[0] = 0;
	modelSentToward[1] = 0;
	modelRepliesToward[0] = 0;
	modelRepliesToward[1] = 0;
	modelLaneTable = {};
	modelPingTable = {};
	modelHeld<Chain> = {};
	modelLabel = Chain::name;

	// The two sides start with consecutive session values: n and n + 1.
	uint16 first = static_cast<uint16>(0x1000u + (seed * 0x9E37u) % 0xE000u);
	modelSessions[0] = first;
	modelSessions[1] = static_cast<uint16>(first + 1);
	modelSessionUsed[0].assign(65536, false);
	modelSessionUsed[1].assign(65536, false);
	modelSessionUsed[0][first] = true;
	modelSessionUsed[1][static_cast<uint16>(first + 1)] = true;

	VUB::Faults faults;
	faults.seed = seed;
	faults.dropPercent = 12;
	faults.corruptPercent = 4;
	// Every chain runs with duplicates and delays of up to 40 ms. A keepsOrder line keeps the order; a reordering line (Reliable chains only)
	// takes the packets apart by their delays.
	faults.duplicatePercent = 8;
	faults.maxDelayUs = 40000;
	faults.keepsOrder = kind == ModelLineKind::keepsOrder;
	ModelCheck(kind == ModelLineKind::keepsOrder || Chain::reliable, "a path with no Reliable below has no reordering line");
	// The rule of a closed request is checked for every chain with Reliable below the requests layer.
	modelHoldsClosed = Chain::reliable;
	modelInOrder = Chain::inOrder;
	modelDuplicates = faults.duplicatePercent > 0;
	modelMaxDelayUs = faults.maxDelayUs;
	ReportingPacketLine line(faults);
	modelLine = &line;

	modelStacks<Chain>[0].emplace(line.a);
	modelStacks<Chain>[1].emplace(line.b);
	ModelGiveSession<Chain>(*modelStacks<Chain>[0], modelSessions[0]);
	ModelGiveSession<Chain>(*modelStacks<Chain>[1], modelSessions[1]);

	for (uint32 warm = 0; warm < 3; warm++) {
		ModelRound<Chain>(50000);
	}

	for (uint32 step = 0; step < modelSteps && !modelViolated; step++) {
		modelStep = step;
		ModelAct<Chain>();
		ModelSweep<Chain>();
		uint32 advance = 1 + modelRandom.Below(20000);
		if (modelRandom.Chance(2)) {
			advance += 2000000 + modelRandom.Below(1000000);
		}

		ModelRound<Chain>(advance);
		modelTotals.steps++;
	}

	modelStep = modelSteps;
	ModelSetLink(true);
	ModelDrain<Chain>(1200);
	ModelFillTables<Chain>(0);
	ModelFillTables<Chain>(1);
	ModelDrain<Chain>(400);
	ModelFinalChecks<Chain>();
	ModelCountFaults();

	modelStacks<Chain>[0].reset();
	modelStacks<Chain>[1].reset();
	modelLine = nullptr;
}


// Prints the totals of one chain.
inline void ModelPrintTotals(const char* name, const char* line, uint32 seeds) {
	std::printf("chain %s on %s: %u seeds, %llu steps, %llu requests, %llu fires, %llu notices, %llu alarms, %llu pings (%llu refused by padding), %llu stale ticket calls, %llu interims, %llu final replies\n",
		name,
		line,
		static_cast<unsigned>(seeds),
		static_cast<unsigned long long>(modelTotals.steps),
		static_cast<unsigned long long>(modelTotals.requests),
		static_cast<unsigned long long>(modelTotals.kicks),
		static_cast<unsigned long long>(modelTotals.notices),
		static_cast<unsigned long long>(modelTotals.alarms),
		static_cast<unsigned long long>(modelTotals.pings),
		static_cast<unsigned long long>(modelTotals.pingRefusals),
		static_cast<unsigned long long>(modelTotals.staleCalls),
		static_cast<unsigned long long>(modelTotals.interimsSent),
		static_cast<unsigned long long>(modelTotals.finalsSent));
	const char* kinds[4] = {"call", "plain", "note", "ping"};
	for (std::size_t kind = 0; kind < 4; kind++) {
		std::printf("  ends %-5s ok %llu deadline %llu cancel %llu restart %llu other %llu\n",
			kinds[kind],
			static_cast<unsigned long long>(modelTotals.ends[kind][BucketOk]),
			static_cast<unsigned long long>(modelTotals.ends[kind][BucketDeadline]),
			static_cast<unsigned long long>(modelTotals.ends[kind][BucketCancel]),
			static_cast<unsigned long long>(modelTotals.ends[kind][BucketRestart]),
			static_cast<unsigned long long>(modelTotals.ends[kind][BucketOther]));
	}

	std::printf("  faults: dropped %llu corrupted %llu duplicated %llu outaged %llu garbage %llu link changes %llu rebuilds %llu\n",
		static_cast<unsigned long long>(modelTotals.dropped),
		static_cast<unsigned long long>(modelTotals.corrupted),
		static_cast<unsigned long long>(modelTotals.duplicated),
		static_cast<unsigned long long>(modelTotals.outaged),
		static_cast<unsigned long long>(modelTotals.garbage),
		static_cast<unsigned long long>(modelTotals.linkChanges),
		static_cast<unsigned long long>(modelTotals.rebuilds));
}


// Runs the seeds of one chain and prints its totals.
template<typename Chain>
void ModelRunChain(uint32 firstSeed, uint32 seedCount, ModelLineKind kind) {
	modelTotals = ModelTotals{};
	for (uint32 index = 0; index < seedCount; index++) {
		ModelRunSeed<Chain>(firstSeed + index, kind);
	}

	ModelPrintTotals(Chain::name, kind == ModelLineKind::keepsOrder ? "keepsOrder" : "reorders", seedCount);
}


// Runs one seed of a chain on its lines: keepsOrder for every chain, and reorders for the Reliable chains.
template<typename Chain>
void ModelRunSeedLines(uint32 seed) {
	ModelRunSeed<Chain>(seed, ModelLineKind::keepsOrder);
	if constexpr (Chain::reliable) {
		ModelRunSeed<Chain>(seed, ModelLineKind::reorders);
	}
}


// Runs the seeds of a chain on the lines the argument names: "all", "keeps" or "reorders".
template<typename Chain>
void ModelRunChainLines(uint32 firstSeed, uint32 seedCount, const std::string& lines) {
	if (lines != "reorders") {
		ModelRunChain<Chain>(firstSeed, seedCount, ModelLineKind::keepsOrder);
	}

	if constexpr (Chain::reliable) {
		if (lines != "keeps") {
			ModelRunChain<Chain>(firstSeed, seedCount, ModelLineKind::reorders);
		}
	}
}


int main(int argc, char** argv) {
	uint32 firstSeed = 1;
	uint32 seedCount = 6;
	if (argc >= 3) {
		firstSeed = static_cast<uint32>(std::strtoul(argv[1], nullptr, 10));
		seedCount = static_cast<uint32>(std::strtoul(argv[2], nullptr, 10));
	}

	if (argc >= 4) {
		modelSteps = static_cast<uint32>(std::strtoul(argv[3], nullptr, 10));
	}

	if (argc < 3) {
		const std::array<uint32, 6> seeds = {1u, 7u, 42u, 1234u, 99991u, 0xC0FFEEu};
		for (uint32 seed : seeds) {
			ModelRunSeedLines<ChainCrc>(seed);
			ModelRunSeedLines<ChainCrcSmall>(seed);
			ModelRunSeedLines<ChainRel>(seed);
			ModelRunSeedLines<ChainRelInOrder>(seed);
			ModelRunSeedLines<ChainRelSmall>(seed);
			ModelRunSeedLines<ChainRelLarge>(seed);
		}

		std::printf("the equal session pair 0x1234 and 0x1234: %s\n", ModelSessionsAllowed(0x1234, 0x1234) ? "allowed" : "not allowed");
		return SimFinish("TestVubRequestsModel");
	}

	// The optional fourth argument names one chain; without it every chain runs.
	std::string filter = argc >= 5 ? std::string(argv[4]) : std::string("all");
	std::string lines = argc >= 6 ? std::string(argv[5]) : std::string("all");
	if (filter == "all" || filter == ChainCrc::name) {
		ModelRunChainLines<ChainCrc>(firstSeed, seedCount, lines);
	}

	if (filter == "all" || filter == ChainCrcSmall::name) {
		ModelRunChainLines<ChainCrcSmall>(firstSeed, seedCount, lines);
	}

	if (filter == "all" || filter == ChainRel::name) {
		ModelRunChainLines<ChainRel>(firstSeed, seedCount, lines);
	}

	if (filter == "all" || filter == ChainRelInOrder::name) {
		ModelRunChainLines<ChainRelInOrder>(firstSeed, seedCount, lines);
	}

	if (filter == "all" || filter == ChainRelSmall::name) {
		ModelRunChainLines<ChainRelSmall>(firstSeed, seedCount, lines);
	}

	if (filter == "all" || filter == ChainRelLarge::name) {
		ModelRunChainLines<ChainRelLarge>(firstSeed, seedCount, lines);
	}

	return SimFinish("TestVubRequestsModel");
}
