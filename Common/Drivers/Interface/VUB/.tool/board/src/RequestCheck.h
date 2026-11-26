#pragma once
#include <Drivers/Interface/VUB/.tool/common/ActuatorCommands.h>
#include <Drivers/Interface/VUB/Requests.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <random>
#include <thread>
#include <vector>


// The requests check of vubcheck --requests. It sends a random sequence of the actuator's requests to the board, and checks what comes
// back against what the board's counters say ran. Each request's final is counted once: the sets, the sweeps, the cancels and the hangs
// carry their token in the reply they finish with, and the other finals are counted per command. The steps of a sweep are checked in order.
//
// Some sweeps are cancelled: right after their send, while the REQUEST is not yet acknowledged; after their first interim; or after
// their final (then the cancel finds no request). A sweep ends once: done, cancelled, or operationCancelled. A finished sweep is announced
// by a Delivered event. The device's ticks are a plain event with a running number, and the PC's notes are plain events the device counts. Pings, with and without padding,
// go out in between and their round trips are timed. The layer's own counters (Counted) are read at the end.
//
// A sweep is sent only when no other sweep is open, so the interims between a sweep's send and its end are its own.
namespace VubBoard {
	namespace RequestsCheck {
		using Status = Actuator::ActuatorStatus;


		// The kinds of request of the plan. Each kind is one command, except absent, which the device does not serve.
		enum class Kind : uint8 {
			set,
			sweep,
			kick,
			nudge,
			hang,
			absent
		};


		constexpr std::size_t KindCount = 6;


		// The categories of a request's final. Refusals are busy, notSupported and invalid; a timeout is the device's ticket time-out or the
		// requester's deadline. cancelled is the layer's operationCancelled; a sweep that its cancel stopped ends ok with the cancelled status.
		enum class Category : uint8 {
			ok,
			timeout,
			busy,
			notSupported,
			invalid,
			reset,
			cancelled,
			other
		};


		constexpr std::size_t CategoryCount = 8;


		// How a sweep is cancelled: not at all, right after its send (while its REQUEST is not yet acknowledged), after its first interim, or
		// after its final, when the cancel must find no request.
		enum class CancelAt : uint8 {
			none,
			beforeAck,
			afterInterim,
			afterFinal
		};


		// The value of an index that names no sweep.
		constexpr std::size_t NoIndex = std::numeric_limits<std::size_t>::max();

		// The pings are sent with the requests whose index is a multiple of PingEvery, and the notes with the ones after a multiple of NoteEvery.
		constexpr std::size_t PingEvery = 5;
		constexpr std::size_t NoteEvery = 4;
		// The padding of the pings: they alternate between none and the largest the layer allows (the device answers with the same maximum).
		constexpr uint16 PingPadding = 16;
		// The deadline of a ping. A ping may need three resends of 200 ms in each direction under --loss, so it is longer than the layer's default.
		constexpr uint32 PingTimeoutMs = 3000;


		// One request of the plan. Its index is its token; a sweep takes its steps, step time and cancel point from here.
		struct Planned {
			Kind kind = Kind::set;
			uint16 level = 0;
			uint8 mode = 0;
			uint8 steps = 0;
			uint16 stepMs = 0;
			CancelAt cancelAt = CancelAt::none;
		};


		// What the run counted for one kind: the requests sent, and the finals by category.
		struct Tally {
			std::size_t sent = 0;
			std::array<std::size_t, CategoryCount> byCategory{};
		};


		// The run's state. The handlers are plain functions, so they write here; one run is one set of values.
		inline std::vector<Planned> plan;
		inline std::vector<VUB::RequestId> ids;
		inline std::vector<bool> seenToken;
		inline std::vector<bool> seenEnd;
		inline std::vector<bool> cancelTried;
		inline std::vector<bool> doneFinal;
		inline std::vector<bool> delivered;
		inline std::array<Tally, KindCount> tallies{};
		inline std::size_t outstanding = 0;
		inline std::size_t fired = 0;
		inline std::size_t duplicate = 0;
		inline std::size_t unknown = 0;
		inline std::size_t mismatched = 0;
		inline std::size_t interims = 0;
		inline std::size_t interimErrors = 0;
		inline bool sweepOpen = false;
		inline std::size_t sweepIndex = 0;
		inline uint8 sweepInterims = 0;
		// The sweep whose final came and whose cancel is due, which must find no request.
		inline std::size_t finalPending = NoIndex;
		inline std::size_t cancelSent = 0;
		inline std::size_t cancelMissed = 0;
		inline std::size_t cancelErrors = 0;
		inline std::size_t cancelledStatus = 0;
		inline std::size_t cancelledFinals = 0;
		inline std::size_t doneFinals = 0;
		inline std::size_t deliveredEvents = 0;
		inline std::size_t pingsSent = 0;
		inline std::size_t pingsOk = 0;
		inline std::size_t pingsFailed = 0;
		inline std::size_t pingsOutstanding = 0;
		inline uint64 pingSumUs = 0;
		inline uint32 pingMinUs = 0;
		inline uint32 pingMaxUs = 0;
		inline std::size_t ticks = 0;
		inline std::size_t tickGaps = 0;
		inline std::size_t tickOrder = 0;
		inline bool tickSeen = false;
		inline uint32 tickLast = 0;
		inline std::size_t notesSent = 0;
		inline bool countersArrived = false;
		inline Actuator::Counters countersLast{};
		inline ResultStatus countersResult = ResultStatus::notAvailable;


		inline Category CategoryOf(ResultStatus result) {
			switch (result) {
				case ResultStatus::ok:
					return Category::ok;

				case ResultStatus::timeout:
					return Category::timeout;

				case ResultStatus::busy:
					return Category::busy;

				case ResultStatus::notSupported:
					return Category::notSupported;

				case ResultStatus::invalidData:
				case ResultStatus::invalidParameter:
					return Category::invalid;

				case ResultStatus::reset:
					return Category::reset;

				case ResultStatus::operationCancelled:
					return Category::cancelled;

				default:
					return Category::other;
			}
		}


		inline std::size_t IndexOf(Kind kind) {
			return static_cast<std::size_t>(kind);
		}


		// A final of a request with a status: its category is counted, and the request is no longer outstanding.
		inline void Finished(Kind kind, ResultStatus result) {
			Tally& tally = tallies[IndexOf(kind)];
			tally.byCategory[static_cast<std::size_t>(CategoryOf(result))]++;
			outstanding--;
		}


		inline void OnSetFinal(const VUB::Final<Status, Actuator::SetReport>& reply) {
			Finished(Kind::set, reply.result);
			if (reply.result != ResultStatus::ok) {
				return;
			}

			if (reply.body.token >= plan.size()) {
				unknown++;
				return;
			}

			const Planned& planned = plan[reply.body.token];
			if (planned.kind != Kind::set || reply.status != Status::applied) {
				mismatched++;
				return;
			}

			if (seenToken[reply.body.token]) {
				duplicate++;
			}

			seenToken[reply.body.token] = true;
			if (reply.body.level != planned.level) {
				mismatched++;
			}
		}


		inline Status StepStatus(uint8 step) {
			return static_cast<Status>(static_cast<uint8>(Status::step1) + step - 1);
		}


		inline void OnSweepInterim(const VUB::Progress<Status>& progress) {
			if (!sweepOpen) {
				unknown++;
				return;
			}

			uint8 step = static_cast<uint8>(sweepInterims + 1);
			if (progress.result != ResultStatus::ok || step > plan[sweepIndex].steps || progress.status != StepStatus(step)) {
				interimErrors++;
			}

			sweepInterims++;
			interims++;
		}


		// The end of a sweep. It comes once per sweep: a second end is a duplicate. A done sweep and a cancelled one each have their own
		// checks; a cancelled end must follow a cancel the PC sent, and it has as many steps as the interims the PC got.
		inline void OnSweepFinal(const VUB::Final<Status, Actuator::SweepReport>& reply) {
			Finished(Kind::sweep, reply.result);
			if (!sweepOpen) {
				unknown++;
				return;
			}

			std::size_t index = sweepIndex;
			const Planned& planned = plan[index];
			if (seenEnd[index]) {
				duplicate++;
			}

			seenEnd[index] = true;
			if (planned.cancelAt == CancelAt::afterFinal) {
				finalPending = index;
			}

			if (reply.result == ResultStatus::ok && reply.status == Status::done) {
				doneFinals++;
				doneFinal[index] = true;
				if (reply.body.token != index || reply.body.steps != planned.steps || sweepInterims != planned.steps) {
					mismatched++;
				}
			} else if (reply.result == ResultStatus::ok && reply.status == Status::cancelled) {
				cancelledStatus++;
				cancelledFinals++;
				if (!cancelTried[index] || reply.body.token != index || reply.body.steps != sweepInterims) {
					mismatched++;
				}
			} else if (reply.result == ResultStatus::operationCancelled) {
				cancelledFinals++;
				if (!cancelTried[index]) {
					mismatched++;
				}
			} else if (reply.result == ResultStatus::ok) {
				mismatched++;
			}

			sweepOpen = false;
		}


		inline void OnKickFinal(ResultStatus result) {
			Finished(Kind::kick, result);
		}


		inline void OnHangFinal(const VUB::Final<Status, VUB::NoBody>& reply) {
			Finished(Kind::hang, reply.result);
		}


		inline void OnAbsentFinal(const VUB::Final<Status, VUB::NoBody>& reply) {
			Finished(Kind::absent, reply.result);
		}


		inline void OnCountersFinal(const VUB::Final<Status, Actuator::Counters>& reply) {
			countersArrived = true;
			countersResult = reply.result;
			if (reply.result == ResultStatus::ok) {
				countersLast = reply.body;
			}
		}


		// The round trip of a ping: the handler of the layer's ping, with its result and the round trip in microseconds.
		inline void OnPingDone(ResultStatus result, uint32 roundTripUs) {
			pingsOutstanding--;
			if (result != ResultStatus::ok) {
				pingsFailed++;
				std::fprintf(stderr, "vubcheck: a ping ended with status %d\n", static_cast<int>(result));
				return;
			}

			pingsOk++;
			pingSumUs += roundTripUs;
			if (pingsOk == 1 || roundTripUs < pingMinUs) {
				pingMinUs = roundTripUs;
			}

			if (roundTripUs > pingMaxUs) {
				pingMaxUs = roundTripUs;
			}
		}


		// A sweep the device finished: its Delivered event. It names a sweep of the plan, with the steps the plan gives it.
		inline void OnSweepDone(const Actuator::SweepReport& report) {
			deliveredEvents++;
			if (report.token >= plan.size()) {
				unknown++;
				return;
			}

			const Planned& planned = plan[report.token];
			if (planned.kind != Kind::sweep || report.steps != planned.steps) {
				mismatched++;
				return;
			}

			if (delivered[report.token]) {
				duplicate++;
			}

			delivered[report.token] = true;
		}


		// A tick of the device: its number rises by one from one tick to the next. A gap is a number the PC did not get; it is allowed
		// only under loss, which the caller checks.
		inline void OnTick(const Actuator::NumberBody& body) {
			ticks++;
			if (tickSeen) {
				if (body.number <= tickLast) {
					tickOrder++;
				} else if (body.number != tickLast + 1) {
					tickGaps++;
				}
			}

			tickSeen = true;
			tickLast = body.number;
		}


		// The handlers of the PC: the device's events. The PC serves no command.
		using PcHandlers = VUB::Handlers<
			VUB::OnEvent<Actuator::ActuatorEvent::sweepDone, &OnSweepDone>,
			VUB::OnEvent<Actuator::ActuatorEvent::tick, &OnTick>>;


		// The plan of the run: count requests, kinds drawn with the weights below, and the sweeps' steps, step times and cancel points, all
		// from the seed.
		inline std::vector<Planned> MakePlan(std::size_t count, uint32 seed) {
			std::mt19937 generator(seed);
			std::uniform_int_distribution<uint32> percent(0, 99);
			std::uniform_int_distribution<uint32> steps(1, Actuator::MaxSweepSteps);
			std::uniform_int_distribution<uint32> stepMs(5, 100);
			std::uniform_int_distribution<uint32> level(0, 65535);
			std::uniform_int_distribution<uint32> mode(0, 255);
			std::vector<Planned> result(count);
			for (Planned& planned : result) {
				uint32 draw = percent(generator);
				if (draw < 30) {
					planned.kind = Kind::set;
				} else if (draw < 45) {
					planned.kind = Kind::sweep;
				} else if (draw < 60) {
					planned.kind = Kind::kick;
				} else if (draw < 75) {
					planned.kind = Kind::nudge;
				} else if (draw < 85) {
					planned.kind = Kind::hang;
				} else {
					planned.kind = Kind::absent;
				}

				planned.level = static_cast<uint16>(level(generator));
				planned.mode = static_cast<uint8>(mode(generator));
				planned.steps = static_cast<uint8>(steps(generator));
				planned.stepMs = static_cast<uint16>(stepMs(generator));
				if (planned.kind == Kind::sweep) {
					uint32 cancelDraw = percent(generator);
					if (cancelDraw < 12) {
						planned.cancelAt = CancelAt::beforeAck;
					} else if (cancelDraw < 24) {
						planned.cancelAt = CancelAt::afterInterim;
					} else if (cancelDraw < 36) {
						planned.cancelAt = CancelAt::afterFinal;
					}
				}
			}

			return result;
		}


		// Keeps the number of an accepted request, and returns the status of the send.
		inline ResultStatus Accepted(Result<VUB::RequestId> sent, std::size_t index) {
			if (sent.IsOk()) {
				ids[index] = sent.Value();
				return ResultStatus::ok;
			}

			return sent.Error();
		}


		// Sends the planned request with the index, through the handle. A send refused by the requester (busy, no slot) returns busy.
		template<typename HandleType>
		ResultStatus SendPlanned(HandleType& handle, std::size_t index) {
			const Planned& planned = plan[index];
			uint32 token = static_cast<uint32>(index);
			switch (planned.kind) {
				case Kind::set: {
					Actuator::SetBody body{};
					body.token = token;
					body.level = planned.level;
					body.mode = planned.mode;
					return Accepted(handle.template Send<Actuator::ActuatorCommand::set, &OnSetFinal>(body), index);
				}

				case Kind::sweep: {
					Actuator::SweepBody body{};
					body.token = token;
					body.steps = planned.steps;
					body.stepMs = planned.stepMs;
					return Accepted(handle.template Send<Actuator::ActuatorCommand::sweep, &OnSweepFinal, &OnSweepInterim>(body), index);
				}

				case Kind::kick: {
					Actuator::TokenBody body{};
					body.token = token;
					return Accepted(handle.template Send<Actuator::ActuatorCommand::kick, &OnKickFinal>(body), index);
				}

				case Kind::nudge: {
					Actuator::TokenBody body{};
					body.token = token;
					return handle.template Fire<Actuator::ActuatorCommand::nudge>(body);
				}

				case Kind::hang: {
					Actuator::TokenBody body{};
					body.token = token;
					return Accepted(handle.template Send<Actuator::ActuatorCommand::hang, &OnHangFinal>(body), index);
				}

				case Kind::absent: {
					return Accepted(handle.template Send<Actuator::ActuatorCommand::absent, &OnAbsentFinal>(VUB::NoBody{}), index);
				}
			}

			return ResultStatus::notSupported;
		}


		// Cancels the sweep with the index. Its end is then the cancelled status, a done end, or operationCancelled; a notFound means the sweep
		// had already ended. Each sweep is cancelled at most once from here.
		template<typename HandleType>
		ResultStatus CancelPlanned(HandleType& handle, std::size_t index) {
			cancelTried[index] = true;
			ResultStatus status = handle.template Cancel<Actuator::ActuatorCommand::sweep>(ids[index]);
			if (status == ResultStatus::ok) {
				cancelSent++;
			} else if (status == ResultStatus::notFound) {
				cancelMissed++;
			} else {
				cancelErrors++;
			}

			return status;
		}


		// Sends the counters request and waits for its final. Returns false when it does not come before the deadline.
		// A busy send is retried: the first send of a session waits for the session's sync, so it is busy for a few rounds.
		template<typename HandleType, typename StackType>
		bool ReadCounters(HandleType& handle, StackType& stack, std::chrono::steady_clock::time_point deadline) {
			countersArrived = false;
			while (true) {
				Result<VUB::RequestId> sent = handle.template Send<Actuator::ActuatorCommand::counters, &OnCountersFinal>(VUB::NoBody{});
				if (sent.IsOk()) {
					break;
				}

				if (sent.Error() != ResultStatus::busy || std::chrono::steady_clock::now() >= deadline) {
					return false;
				}

				stack.Process();
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}

			while (!countersArrived && std::chrono::steady_clock::now() < deadline) {
				stack.Process();
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}

			return countersArrived && countersResult == ResultStatus::ok;
		}


		inline std::size_t CountOf(Kind kind, Category category) {
			return tallies[IndexOf(kind)].byCategory[static_cast<std::size_t>(category)];
		}


		inline std::size_t Sent(Kind kind) {
			return tallies[IndexOf(kind)].sent;
		}


		inline std::size_t Total(Category category) {
			std::size_t total = 0;
			for (const Tally& tally : tallies) {
				total += tally.byCategory[static_cast<std::size_t>(category)];
			}

			return total;
		}


		// Each category a kind may end with. A category outside these is a failure: the set ends ok or refused busy, the sweep ends ok (done or
		// cancelled), busy or operationCancelled, the hang ends with its time-out or a refusal, the absent command is refused as not
		// supported, and the kick ends ok.
		inline bool Expected(Kind kind, Category category) {
			switch (kind) {
				case Kind::set:
					return category == Category::ok || category == Category::busy;

				case Kind::sweep:
					return category == Category::ok || category == Category::busy || category == Category::cancelled;

				case Kind::kick:
					return category == Category::ok;

				case Kind::nudge:
					return false;

				case Kind::hang:
					return category == Category::timeout || category == Category::busy;

				case Kind::absent:
					return category == Category::notSupported;
			}

			return false;
		}


		inline bool AllExpected() {
			for (std::size_t index = 0; index < KindCount; index++) {
				Kind kind = static_cast<Kind>(index);
				for (std::size_t category = 0; category < CategoryCount; category++) {
					std::size_t count = tallies[index].byCategory[category];
					if (count > 0 && !Expected(kind, static_cast<Category>(category))) {
						return false;
					}
				}
			}

			return true;
		}


		// With no loss, each token has a Delivered event exactly when its sweep ended done.
		inline bool DeliveredMatchesDone() {
			for (std::size_t index = 0; index < plan.size(); index++) {
				if (delivered[index] != doneFinal[index]) {
					return false;
				}
			}

			return true;
		}


		// Resets the run's state for count requests, and makes the plan from the seed.
		inline void ResetRun(std::size_t count, uint32 seed) {
			plan = MakePlan(count, seed);
			ids.assign(count, VUB::RequestId{});
			seenToken.assign(count, false);
			seenEnd.assign(count, false);
			cancelTried.assign(count, false);
			doneFinal.assign(count, false);
			delivered.assign(count, false);
			tallies = {};
			outstanding = 0;
			fired = 0;
			duplicate = 0;
			unknown = 0;
			mismatched = 0;
			interims = 0;
			interimErrors = 0;
			sweepOpen = false;
			sweepIndex = 0;
			sweepInterims = 0;
			finalPending = NoIndex;
			cancelSent = 0;
			cancelMissed = 0;
			cancelErrors = 0;
			cancelledStatus = 0;
			cancelledFinals = 0;
			doneFinals = 0;
			deliveredEvents = 0;
			pingsSent = 0;
			pingsOk = 0;
			pingsFailed = 0;
			pingsOutstanding = 0;
			pingSumUs = 0;
			pingMinUs = 0;
			pingMaxUs = 0;
			ticks = 0;
			tickGaps = 0;
			tickOrder = 0;
			tickSeen = false;
			tickLast = 0;
			notesSent = 0;
		}
	}


	// Runs the requests check on the stack, and prints the summary line. The requester's link is the one of the stack; loss is the
	// --loss value (0 for none). Returns the exit code: 0 for PASS, 1 for FAIL.
	template<typename StackType, typename LinkType>
	int RunRequests(
		StackType& stack,
		LinkType& link,
		const char* linkName,
		std::size_t count,
		std::size_t inflight,
		std::size_t loss,
		std::size_t timeoutSeconds,
		uint32 seed)
	{
		using namespace RequestsCheck;
		link.SetDropEvery(loss);
		ResetRun(count, seed);
		std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
		std::chrono::steady_clock::time_point deadline = start + std::chrono::seconds(timeoutSeconds);
		auto handle = stack.Requests();

		// The counters before the run. The device's counters count from its last reset, so the run compares the difference.
		bool baselineOk = ReadCounters(handle, stack, deadline);
		Actuator::Counters before = countersLast;

		std::size_t nextSend = 0;
		std::size_t pingCount = 0;
		ResultStatus sendError = ResultStatus::ok;
		bool timedOut = false;
		bool pingDue = false;
		bool noteDue = false;
		while (baselineOk) {
			if (nextSend < plan.size() && outstanding < inflight) {
				bool blockedBySweep = plan[nextSend].kind == Kind::sweep && sweepOpen;
				if (!blockedBySweep) {
					ResultStatus status = SendPlanned(handle, nextSend);
					if (status == ResultStatus::ok) {
						Kind kind = plan[nextSend].kind;
						tallies[IndexOf(kind)].sent++;
						if (kind == Kind::nudge) {
							fired++;
						} else {
							outstanding++;
						}

						if (kind == Kind::sweep) {
							sweepOpen = true;
							sweepIndex = nextSend;
							sweepInterims = 0;
							if (plan[nextSend].cancelAt == CancelAt::beforeAck) {
								static_cast<void>(CancelPlanned(handle, nextSend));
							}
						}

						if (nextSend % PingEvery == 0) {
							pingDue = true;
						}

						if (nextSend % NoteEvery == 1) {
							noteDue = true;
						}

						nextSend++;
					} else if (status != ResultStatus::busy) {
						sendError = status;
						break;
					}
				}
			}

			// The pings are sent one at a time: the device holds one reply, and a second ping that arrives while that reply is unsent is
			// refused busy (the layer's refusal ring). A ping that is due waits for the one before it to end.
			if (pingDue && pingsOutstanding == 0) {
				uint16 padding = pingCount % 2 == 0 ? 0 : PingPadding;
				ResultStatus status = handle.template Ping<&OnPingDone>(padding);
				if (status == ResultStatus::ok) {
					pingsSent++;
					pingsOutstanding++;
					pingCount++;
					pingDue = false;
				} else if (status != ResultStatus::busy) {
					sendError = status;
					break;
				}
			}

			if (noteDue) {
				Actuator::NumberBody body{};
				body.number = static_cast<uint32>(notesSent + 1);
				ResultStatus status = handle.template Emit<Actuator::ActuatorEvent::note>(body);
				if (status == ResultStatus::ok) {
					notesSent++;
					noteDue = false;
				} else if (status != ResultStatus::busy) {
					sendError = status;
					break;
				}
			}

			stack.Process();
			if (finalPending != NoIndex) {
				std::size_t index = finalPending;
				finalPending = NoIndex;
				// The sweep has ended, so its cancel finds no request: any other answer is a fault.
				if (handle.template Cancel<Actuator::ActuatorCommand::sweep>(ids[index]) != ResultStatus::notFound) {
					mismatched++;
				}
			}

			if (sweepOpen && plan[sweepIndex].cancelAt == CancelAt::afterInterim && sweepInterims >= 1 && !cancelTried[sweepIndex]) {
				static_cast<void>(CancelPlanned(handle, sweepIndex));
			}

			if (nextSend == plan.size() && outstanding == 0 && !sweepOpen && !pingDue && !noteDue && pingsOutstanding == 0 &&
				finalPending == NoIndex) {
				break;
			}

			if (std::chrono::steady_clock::now() >= deadline) {
				timedOut = true;
				break;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		// The Fire packets and the events have no final, so the run waits a short time for their packets before the counters are read.
		std::chrono::steady_clock::time_point drainEnd = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
		while (std::chrono::steady_clock::now() < drainEnd) {
			stack.Process();
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		bool endOk = baselineOk && ReadCounters(handle, stack, deadline + std::chrono::seconds(2));
		Actuator::Counters after = countersLast;
		double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

		uint32 setsDelta = after.sets - before.sets;
		uint32 sweepsDelta = after.sweeps - before.sweeps;
		uint32 kicksDelta = after.kicks - before.kicks;
		uint32 nudgesDelta = after.nudges - before.nudges;
		uint32 hangsDelta = after.hangs - before.hangs;
		uint32 cancelsDelta = static_cast<uint16>(after.cancels - before.cancels);
		uint32 notesDelta = static_cast<uint16>(after.notes - before.notes);
		std::size_t hangsAdmitted = Sent(Kind::hang) - CountOf(Kind::hang, Category::busy);

		bool countersMatch = setsDelta == CountOf(Kind::set, Category::ok) && kicksDelta == CountOf(Kind::kick, Category::ok);
		// A fire has no final, so under loss its packet may not arrive: the device then counts fewer than were sent. Without loss the counts
		// are equal, and the hangs that were admitted are the ones that time out.
		bool nudgesMatch = loss == 0 ? nudgesDelta == fired : nudgesDelta <= fired;
		bool hangsMatch = loss == 0 ? hangsDelta == hangsAdmitted : hangsDelta <= hangsAdmitted;
		// A done sweep has its count on the device and its Delivered event. Under loss the done final or the event may be lost after the
		// device counted it, so only the bounds hold.
		bool sweepsMatch = loss == 0 ? sweepsDelta == doneFinals : doneFinals <= sweepsDelta;
		bool eventsMatch = loss == 0 ? deliveredEvents == sweepsDelta && DeliveredMatchesDone() : deliveredEvents <= sweepsDelta;
		// Each cancel runs its handler at most once, and a cancelled end has run one. A cancel that the device never ran is not counted.
		bool cancelsMatch = cancelsDelta <= cancelSent && cancelledStatus <= cancelsDelta;
		bool notesMatch = loss == 0 ? notesDelta == notesSent : notesDelta <= notesSent;
		bool pingsMatch = pingsSent > 0 && pingsOk == pingsSent && pingsFailed == 0 && pingsOutstanding == 0;
		// The ticks rise by one. A gap is allowed only under loss. Without loss the run has about two ticks a second, one of them missed at
		// most at the ends.
		bool ticksMatch = tickOrder == 0 && (loss != 0 || (tickGaps == 0 && ticks + 2 >= static_cast<std::size_t>(seconds * 2.0)));
		// The layer's counters: no refusal and no lost event on the PC, and no late reply without loss.
		uint16 refused = handle.Refused();
		uint16 lateReplies = handle.LateReplies();
		uint16 eventsLost = handle.EventsLost();
		bool countedMatch = refused == 0 && eventsLost == 0 && (loss != 0 || lateReplies == 0);

		bool pass = sendError == ResultStatus::ok && !timedOut && baselineOk && endOk && nextSend == plan.size() && outstanding == 0 &&
			!sweepOpen && AllExpected() && duplicate == 0 && unknown == 0 && mismatched == 0 && interimErrors == 0 && cancelErrors == 0 &&
			countersMatch && sweepsMatch && eventsMatch && cancelsMatch && notesMatch && nudgesMatch && hangsMatch && pingsMatch &&
			ticksMatch && countedMatch && finalPending == NoIndex && Total(Category::other) == 0 && Total(Category::reset) == 0 &&
			Total(Category::invalid) == 0;

		if (sendError != ResultStatus::ok) {
			std::fprintf(stderr, "vubcheck: the requester refused a request with status %d\n", static_cast<int>(sendError));
		}

		std::size_t finals = 0;
		for (std::size_t index = 0; index < KindCount; index++) {
			for (std::size_t category = 0; category < CategoryCount; category++) {
				finals += tallies[index].byCategory[category];
			}
		}

		std::printf(
			"vubcheck link=%s requests sent=%zu finals=%zu ok=%zu timeout=%zu busy=%zu notSupported=%zu cancelled=%zu invalid=%zu "
			"other=%zu duplicate=%zu unknown=%zu mismatched=%zu interims=%zu interimErrors=%zu sets=%zu/%u sweeps=%zu/%u "
			"delivered=%zu cancelSent=%zu cancelRuns=%u cancelledEnds=%zu cancelMissed=%zu kicks=%zu/%u nudges=%zu/%u hangs=%zu/%u "
			"notes=%zu/%u ticks=%zu tickGaps=%zu pings=%zu/%zu pingFailed=%zu rttMinUs=%u rttAvgUs=%.0f rttMaxUs=%u refused=%u "
			"lateReplies=%u eventsLost=%u dropped=%zu inflight=%zu loss=%zu seed=%lu seconds=%.1f result=%s\n",
			linkName, count, finals, Total(Category::ok), Total(Category::timeout), Total(Category::busy), Total(Category::notSupported),
			Total(Category::cancelled), Total(Category::invalid), Total(Category::other), duplicate, unknown, mismatched, interims,
			interimErrors, CountOf(Kind::set, Category::ok), setsDelta, doneFinals, sweepsDelta, deliveredEvents, cancelSent, cancelsDelta,
			cancelledFinals, cancelMissed, CountOf(Kind::kick, Category::ok), kicksDelta, fired, nudgesDelta, hangsAdmitted, hangsDelta,
			notesSent, notesDelta, ticks, tickGaps, pingsOk, pingsSent, pingsFailed,
			pingsOk > 0 ? pingMinUs : 0U,
			pingsOk > 0 ? static_cast<double>(pingSumUs) / static_cast<double>(pingsOk) : 0.0,
			pingsOk > 0 ? pingMaxUs : 0U,
			static_cast<unsigned>(refused), static_cast<unsigned>(lateReplies), static_cast<unsigned>(eventsLost),
			static_cast<std::size_t>(link.Dropped()), inflight, loss, static_cast<unsigned long>(seed), seconds, pass ? "PASS" : "FAIL");
		return pass ? 0 : 1;
	}
}
