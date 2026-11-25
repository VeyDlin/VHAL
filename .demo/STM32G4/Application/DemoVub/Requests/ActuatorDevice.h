#pragma once
#include <Drivers/Interface/VUB/.tool/common/ActuatorCommands.h>
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/Layer/Reliable.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Drivers/Interface/VUB/Transform/Crc.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <span>


// The device side of the requests demo: a simulated actuator. Its handlers run from the stacks' Process; a sweep answers in steps over
// time, and Tick sends the steps and the finals from the main loop. The counters count what the handlers ran, so the PC can compare
// them with the replies it got.
//
// A sweep can be cancelled. Its cancel handler stops the sweep and finishes the ticket with the cancelled status. A finished sweep
// (done) is announced with a Delivered event, and a tick event with a running number goes out every TickMs while a requester is
// connected. The PC's note event is counted in the counters reply.
//
// ActuatorClockUs is the microsecond clock of the stacks. The board defines it in DemoVub.cpp and the host loopback test in its own file.
namespace DemoVub {


using SetTicket = VUB::Ticket<Actuator::ActuatorCommands, Actuator::ActuatorCommand::set>;
using SweepTicket = VUB::Ticket<Actuator::ActuatorCommands, Actuator::ActuatorCommand::sweep>;
using HangTicket = VUB::Ticket<Actuator::ActuatorCommands, Actuator::ActuatorCommand::hang>;
using CountersTicket = VUB::Ticket<Actuator::ActuatorCommands, Actuator::ActuatorCommand::counters>;

// The sweeps the device runs at once. Each stack has four tickets, so a sweep that holds one of them is one of eight.
constexpr std::size_t SweepSlots = 8;
// The index of each stack that serves the actuator. A sweep and a tick belong to the stack that carries them.
constexpr std::size_t UartStackIndex = 0;
constexpr std::size_t CdcStackIndex = 1;
constexpr std::size_t ServingStacks = 2;
// The time between two ticks, in milliseconds.
constexpr uint32 TickMs = 500;


// One sweep in progress: its ticket, the steps given so far, and the time of the next step or of the final. A finished sweep keeps its
// report until its Delivered event is sent (reportPending), so its slot is kept until then.
struct SweepJob {
	SweepTicket ticket;
	bool active = false;
	bool reportPending = false;
	uint8 stack = 0;
	uint8 steps = 0;
	uint8 given = 0;
	uint16 stepMs = 0;
	uint32 token = 0;
	uint32 dueMs = 0;
	Actuator::SweepReport report{};
};


// The running number of the ticks one stack sends, and the time the next one is due.
struct TickState {
	uint32 number = 0;
	uint32 dueMs = 0;
};


// The counters of the device, and the sweep state the status block shows. unclaimed counts the payloads the requests layer did not take.
struct ActuatorState {
	Actuator::Counters counters{};
	uint32 interims = 0;
	uint32 interimBusy = 0;
	uint32 sweepsExpired = 0;
	uint32 sweepsActive = 0;
	uint32 unclaimed = 0;
	uint32 eventBusy = 0;
};


inline ActuatorState state;
inline std::array<SweepJob, SweepSlots> sweeps{};
inline std::array<TickState, ServingStacks> ticks{};


uint32 ActuatorClockUs();


// The status of step number step (1 to MaxSweepSteps) of a sweep.
inline Actuator::ActuatorStatus StepStatus(uint8 step) {
	return static_cast<Actuator::ActuatorStatus>(static_cast<uint8>(Actuator::ActuatorStatus::step1) + step - 1);
}


// A slot that is neither running nor waiting for its Delivered event.
inline SweepJob* FreeSweep() {
	for (SweepJob& job : sweeps) {
		if (!job.active && !job.reportPending) {
			return &job;
		}
	}

	return nullptr;
}


// The running sweep of a stack with the request number. A cancel names its sweep this way: the numbers are unique on one stack.
inline SweepJob* FindSweep(std::size_t stack, uint16 number) {
	for (SweepJob& job : sweeps) {
		if (job.active && job.stack == stack && job.ticket.Number().number == number) {
			return &job;
		}
	}

	return nullptr;
}


void OnSet(SetTicket ticket, const Actuator::SetBody& body) {
	state.counters.sets++;
	Actuator::SetReport report{};
	report.token = body.token;
	report.level = body.level;
	report.sets = state.counters.sets;
	static_cast<void>(ticket.Finish(Actuator::ActuatorStatus::applied, report));
}


// The sweep takes the ticket and gives its steps from Tick. A sweep with no free job is not taken: with eight jobs and eight tickets
// over the two stacks, that does not happen, and the ticket times out if it does.
template<std::size_t Stack>
void OnSweep(SweepTicket ticket, const Actuator::SweepBody& body) {
	SweepJob* job = FreeSweep();
	if (job == nullptr) {
		return;
	}

	job->ticket = ticket;
	job->active = true;
	job->stack = static_cast<uint8>(Stack);
	job->steps = body.steps < 1 ? 1 : (body.steps > Actuator::MaxSweepSteps ? Actuator::MaxSweepSteps : body.steps);
	job->stepMs = body.stepMs < 1 ? 1 : (body.stepMs > Actuator::MaxStepMs ? Actuator::MaxStepMs : body.stepMs);
	job->given = 0;
	job->token = body.token;
	job->dueMs = 0;
	state.sweepsActive++;
}


// The cancel handler: it runs once per ticket, and it stops the sweep and finishes the ticket with the cancelled status. A ticket with no
// sweep of this device (its sweep was not taken) is left open, and the layer answers it with operationCancelled.
template<std::size_t Stack>
void OnSweepCancel(SweepTicket ticket) {
	state.counters.cancels++;
	SweepJob* job = FindSweep(Stack, ticket.Number().number);
	if (job == nullptr) {
		return;
	}

	Actuator::SweepReport report{};
	report.token = job->token;
	report.steps = job->given;
	report.sweeps = state.counters.sweeps;
	static_cast<void>(ticket.Finish(Actuator::ActuatorStatus::cancelled, report));
	job->active = false;
	state.sweepsActive--;
}


void OnKick(const Actuator::TokenBody& body) {
	(void)body;
	state.counters.kicks++;
}


void OnNudge(const Actuator::TokenBody& body) {
	(void)body;
	state.counters.nudges++;
}


// The handler takes the request and never finishes it: the ticket's time-out answers the PC with timeout.
void OnHang(HangTicket ticket, const Actuator::TokenBody& body) {
	(void)ticket;
	(void)body;
	state.counters.hangs++;
}


void OnCounters(CountersTicket ticket) {
	static_cast<void>(ticket.Finish(Actuator::ActuatorStatus::applied, state.counters));
}


// The PC's note: counted, and its number is not used.
void OnNote(const Actuator::NumberBody& body) {
	(void)body;
	state.counters.notes++;
}


// Gives the steps and the finals that are due. Step k goes out at (k - 1) steps' time, and the final at the time of the last step. A step the
// ticket cannot take yet (an interim not sent) is given on a later call, so the time is kept. A final of a sweep that ran to its end waits
// for ServeEvents to send its Delivered event.
inline void Tick(uint32 nowMs) {
	for (SweepJob& job : sweeps) {
		if (!job.active) {
			continue;
		}

		if (!job.ticket.Open()) {
			job.active = false;
			state.sweepsActive--;
			state.sweepsExpired++;
			continue;
		}

		if (static_cast<int32>(nowMs - job.dueMs) < 0) {
			continue;
		}

		if (job.given < job.steps) {
			if (job.ticket.Interim(StepStatus(static_cast<uint8>(job.given + 1))) != ResultStatus::ok) {
				state.interimBusy++;
				continue;
			}

			job.given++;
			state.interims++;
			job.dueMs = nowMs + job.stepMs;
			continue;
		}

		Actuator::SweepReport report{};
		report.token = job.token;
		report.steps = job.steps;
		report.sweeps = state.counters.sweeps + 1;
		if (job.ticket.Finish(Actuator::ActuatorStatus::done, report) != ResultStatus::ok) {
			continue;
		}

		state.counters.sweeps++;
		job.report = report;
		job.reportPending = true;
		job.active = false;
		state.sweepsActive--;
	}
}


// True when the stack's requester is connected. A link that reports no state is always connected.
template<typename StackType>
bool Connected(const StackType& stack) {
	if constexpr (requires { stack.LinkUp(); }) {
		return stack.LinkUp();
	} else {
		return true;
	}
}


// Sends the events of one stack: the Delivered events of its finished sweeps, then the tick when it is due and the requester is connected.
// A refused send is kept for the next call, so an event is not lost and a number is not skipped.
template<std::size_t Stack, typename StackType>
inline void ServeEvents(StackType& stack, uint32 nowMs) {
	auto handle = stack.Requests();
	for (SweepJob& job : sweeps) {
		if (!job.reportPending || job.stack != Stack) {
			continue;
		}

		if (handle.template Emit<Actuator::ActuatorEvent::sweepDone>(job.report) != ResultStatus::ok) {
			state.eventBusy++;
			break;
		}

		job.reportPending = false;
	}

	TickState& tick = ticks[Stack];
	if (!Connected(stack) || static_cast<int32>(nowMs - tick.dueMs) < 0) {
		return;
	}

	Actuator::NumberBody body{};
	body.number = tick.number + 1;
	if (handle.template Emit<Actuator::ActuatorEvent::tick>(body) != ResultStatus::ok) {
		state.eventBusy++;
		return;
	}

	tick.number++;
	tick.dueMs = nowMs + TickMs;
}


// The receive handler of the stacks: the requests layer takes the packets of the command set, so this gets only the payloads it does not take.
void OnPayload(const VUB::Meta&, std::span<const uint8> payload) {
	(void)payload;
	state.unclaimed++;
}


// The handlers of one stack. The sweep handlers carry the stack's index, so a cancel finds the sweep of its own stack.
template<std::size_t Stack>
using DeviceHandlersOf = VUB::Handlers<
	VUB::On<Actuator::ActuatorCommand::set, &OnSet>,
	VUB::On<Actuator::ActuatorCommand::sweep, &OnSweep<Stack>>,
	VUB::OnCancel<Actuator::ActuatorCommand::sweep, &OnSweepCancel<Stack>>,
	VUB::On<Actuator::ActuatorCommand::kick, &OnKick>,
	VUB::On<Actuator::ActuatorCommand::nudge, &OnNudge>,
	VUB::On<Actuator::ActuatorCommand::hang, &OnHang>,
	VUB::On<Actuator::ActuatorCommand::counters, &OnCounters>,
	VUB::OnEvent<Actuator::ActuatorEvent::note, &OnNote>>;


// The chain of the device's UART: the requests over Reliable, CRC-16/MODBUS, then COBS. Four tickets; the payload is 32 bytes, the
// largest packet of the command set. The layer answers the pings of the PC.
template<typename LinkType>
struct UartActuatorStack {
	using Type = VUB::Stack<+[]() {
		return VUB::Builder()
			.Layer<VUB::Requests<Actuator::ActuatorCommands, DeviceHandlersOf<UartStackIndex>>>().Tickets(4).AnswerPing(16)
			.Layer<VUB::Reliable>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(32)
			.Transform<VUB::Crc16Modbus>()
			.Framing<VUB::Cobs>()
			.template Link<LinkType>()
			.MaxPayload(32)
			.template OnReceive<&OnPayload>()
			.template Clock<&ActuatorClockUs>();
	}>;
};


// The chain of the device's CDC-ACM serial port: the requests over Reliable, then COBS. The layer answers the pings of the PC.
template<typename LinkType>
struct CdcActuatorStack {
	using Type = VUB::Stack<+[]() {
		return VUB::Builder()
			.Layer<VUB::Requests<Actuator::ActuatorCommands, DeviceHandlersOf<CdcStackIndex>>>().Tickets(4).AnswerPing(16)
			.Layer<VUB::Reliable>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(32)
			.Framing<VUB::Cobs>()
			.template Link<LinkType>()
			.MaxPayload(32)
			.template OnReceive<&OnPayload>()
			.template Clock<&ActuatorClockUs>();
	}>;
};

}
