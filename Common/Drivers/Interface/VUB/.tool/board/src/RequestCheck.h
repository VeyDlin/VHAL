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
#include <random>
#include <thread>
#include <vector>


// The requests check of vubcheck --requests. It sends a random sequence of the actuator's requests to the board, and checks what comes
// back against what the board's counters say ran. Each request's final is counted once: the sets, the sweeps and the hangs carry their
// token in the reply they finish with, and the other finals are counted per command. The steps of a sweep are checked in order.
//
// A sweep is sent only when no other sweep is open, so the interims between a sweep's send and its final are its own.
namespace VubBoard {
	namespace RequestsCheck {
		using Command = Actuator::ActuatorCommands;
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
		// requester's deadline.
		enum class Category : uint8 {
			ok,
			timeout,
			busy,
			notSupported,
			invalid,
			reset,
			other
		};


		constexpr std::size_t CategoryCount = 7;


		// One request of the plan. Its index is its token; a sweep takes its steps and step time from here.
		struct Planned {
			Kind kind = Kind::set;
			uint16 level = 0;
			uint8 mode = 0;
			uint8 steps = 0;
			uint16 stepMs = 0;
		};


		// What the run counted for one kind: the requests sent, and the finals by category.
		struct Tally {
			std::size_t sent = 0;
			std::array<std::size_t, CategoryCount> byCategory{};
		};


		// The run's state. The handlers are plain functions, so they write here; one run is one set of values.
		inline std::vector<Planned> plan;
		inline std::vector<bool> seenToken;
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


		inline void OnSweepFinal(const VUB::Final<Status, Actuator::SweepReport>& reply) {
			Finished(Kind::sweep, reply.result);
			if (!sweepOpen) {
				unknown++;
				return;
			}

			if (reply.result == ResultStatus::ok) {
				const Planned& planned = plan[sweepIndex];
				if (reply.status != Status::done || reply.body.token != sweepIndex || reply.body.steps != planned.steps ||
					sweepInterims != planned.steps) {
					mismatched++;
				}
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


		// The plan of the run: count requests, kinds drawn with the weights below, and the sweeps' steps and step times, all from the seed.
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
			}

			return result;
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
					Result<VUB::RequestId> sent = handle.template Send<"actuator.set", &OnSetFinal>(body);
					return sent.IsOk() ? ResultStatus::ok : sent.Error();
				}

				case Kind::sweep: {
					Actuator::SweepBody body{};
					body.token = token;
					body.steps = planned.steps;
					body.stepMs = planned.stepMs;
					Result<VUB::RequestId> sent = handle.template Send<"actuator.sweep", &OnSweepFinal, &OnSweepInterim>(body);
					return sent.IsOk() ? ResultStatus::ok : sent.Error();
				}

				case Kind::kick: {
					Actuator::TokenBody body{};
					body.token = token;
					Result<VUB::RequestId> sent = handle.template Send<"actuator.kick", &OnKickFinal>(body);
					return sent.IsOk() ? ResultStatus::ok : sent.Error();
				}

				case Kind::nudge: {
					Actuator::TokenBody body{};
					body.token = token;
					return handle.template Fire<"actuator.nudge">(body);
				}

				case Kind::hang: {
					Actuator::TokenBody body{};
					body.token = token;
					Result<VUB::RequestId> sent = handle.template Send<"actuator.hang", &OnHangFinal>(body);
					return sent.IsOk() ? ResultStatus::ok : sent.Error();
				}

				case Kind::absent: {
					Result<VUB::RequestId> sent = handle.template Send<"actuator.absent", &OnAbsentFinal>(VUB::NoBody{});
					return sent.IsOk() ? ResultStatus::ok : sent.Error();
				}
			}

			return ResultStatus::notSupported;
		}


		// Sends the counters request and waits for its final. Returns false when it does not come before the deadline.
		// A busy send is retried: the first send of a session waits for the session's sync, so it is busy for a few rounds.
		template<typename HandleType, typename StackType>
		bool ReadCounters(HandleType& handle, StackType& stack, std::chrono::steady_clock::time_point deadline) {
			countersArrived = false;
			while (true) {
				Result<VUB::RequestId> sent = handle.template Send<"actuator.counters", &OnCountersFinal>(VUB::NoBody{});
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


		// Each category a kind may end with. A category outside these is a failure: the set, the sweep and the counters end ok or refused busy,
		// the hang ends with its time-out or a refusal, the absent command is refused as not supported, and the kick ends ok.
		inline bool Expected(Kind kind, Category category) {
			switch (kind) {
				case Kind::set:
				case Kind::sweep:
					return category == Category::ok || category == Category::busy;

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


		// Resets the run's state for count requests, and makes the plan from the seed.
		inline void ResetRun(std::size_t count, uint32 seed) {
			plan = MakePlan(count, seed);
			seenToken.assign(count, false);
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
		ResultStatus sendError = ResultStatus::ok;
		bool timedOut = false;
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
						}

						nextSend++;
					} else if (status != ResultStatus::busy) {
						sendError = status;
						break;
					}
				}
			}

			stack.Process();
			if (nextSend == plan.size() && outstanding == 0 && !sweepOpen) {
				break;
			}

			if (std::chrono::steady_clock::now() >= deadline) {
				timedOut = true;
				break;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		// The Fire packets have no final, so the run waits a short time for their packets before the counters are read.
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
		std::size_t hangsAdmitted = Sent(Kind::hang) - CountOf(Kind::hang, Category::busy);

		bool countersMatch = setsDelta == CountOf(Kind::set, Category::ok) && sweepsDelta == CountOf(Kind::sweep, Category::ok) &&
			kicksDelta == CountOf(Kind::kick, Category::ok);
		// A fire has no final, so under loss its packet may not arrive: the device then counts fewer than were sent. Without loss the counts
		// are equal, and the hangs that were admitted are the ones that time out.
		bool nudgesMatch = loss == 0 ? nudgesDelta == fired : nudgesDelta <= fired;
		bool hangsMatch = loss == 0 ? hangsDelta == hangsAdmitted : hangsDelta <= hangsAdmitted;

		bool pass = sendError == ResultStatus::ok && !timedOut && baselineOk && endOk && nextSend == plan.size() && outstanding == 0 &&
			!sweepOpen && AllExpected() && duplicate == 0 && unknown == 0 && mismatched == 0 && interimErrors == 0 && countersMatch &&
			nudgesMatch && hangsMatch && Total(Category::other) == 0 && Total(Category::reset) == 0 && Total(Category::invalid) == 0;

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
			"vubcheck link=%s requests sent=%zu finals=%zu ok=%zu timeout=%zu busy=%zu notSupported=%zu invalid=%zu other=%zu "
			"duplicate=%zu unknown=%zu mismatched=%zu interims=%zu interimErrors=%zu sets=%zu/%u sweeps=%zu/%u kicks=%zu/%u "
			"nudges=%zu/%u hangs=%zu/%u dropped=%zu inflight=%zu loss=%zu seed=%lu seconds=%.1f result=%s\n",
			linkName, count, finals, Total(Category::ok), Total(Category::timeout), Total(Category::busy), Total(Category::notSupported),
			Total(Category::invalid), Total(Category::other), duplicate, unknown, mismatched, interims, interimErrors,
			CountOf(Kind::set, Category::ok), setsDelta, CountOf(Kind::sweep, Category::ok), sweepsDelta,
			CountOf(Kind::kick, Category::ok), kicksDelta, fired, nudgesDelta, hangsAdmitted, hangsDelta,
			static_cast<std::size_t>(link.Dropped()), inflight, loss, static_cast<unsigned long>(seed), seconds, pass ? "PASS" : "FAIL");
		return pass ? 0 : 1;
	}
}
