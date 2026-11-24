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
// time, and Tick sends the steps and the final from the main loop. The counters count what the handlers ran, so the PC can compare
// them with the replies it got.
//
// ActuatorClockUs is the microsecond clock of the stacks. The board defines it in DemoVub.cpp and the host loopback test in its own file.
namespace DemoVub {

using SetTicket = VUB::Ticket<Actuator::ActuatorCommands, "actuator.set">;
using SweepTicket = VUB::Ticket<Actuator::ActuatorCommands, "actuator.sweep">;
using HangTicket = VUB::Ticket<Actuator::ActuatorCommands, "actuator.hang">;
using CountersTicket = VUB::Ticket<Actuator::ActuatorCommands, "actuator.counters">;

// The sweeps the device runs at once. Each stack has four tickets, so a sweep that holds one of them is one of eight.
constexpr std::size_t SweepSlots = 8;


// One sweep in progress: its ticket, the steps given so far, and the time of the next step or of the final.
struct SweepJob {
	SweepTicket ticket;
	bool active = false;
	uint8 steps = 0;
	uint8 given = 0;
	uint16 stepMs = 0;
	uint32 token = 0;
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
};


inline ActuatorState state;
inline std::array<SweepJob, SweepSlots> sweeps{};


uint32 ActuatorClockUs();


// The status of step number step (1 to MaxSweepSteps) of a sweep.
inline Actuator::ActuatorStatus StepStatus(uint8 step) {
	return static_cast<Actuator::ActuatorStatus>(static_cast<uint8>(Actuator::ActuatorStatus::step1) + step - 1);
}


inline SweepJob* FreeSweep() {
	for (SweepJob& job : sweeps) {
		if (!job.active) {
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
void OnSweep(SweepTicket ticket, const Actuator::SweepBody& body) {
	SweepJob* job = FreeSweep();
	if (job == nullptr) {
		return;
	}

	job->ticket = ticket;
	job->active = true;
	job->steps = body.steps < 1 ? 1 : (body.steps > Actuator::MaxSweepSteps ? Actuator::MaxSweepSteps : body.steps);
	job->stepMs = body.stepMs < 1 ? 1 : (body.stepMs > Actuator::MaxStepMs ? Actuator::MaxStepMs : body.stepMs);
	job->given = 0;
	job->token = body.token;
	job->dueMs = 0;
	state.sweepsActive++;
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


// Gives the steps and the finals that are due. Step k goes out at (k - 1) steps' time, and the final at the time of the last step. A step the
// ticket cannot take yet (an interim not sent) is given on a later call, so the time is kept.
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

		state.counters.sweeps++;
		Actuator::SweepReport report{};
		report.token = job.token;
		report.steps = job.steps;
		report.sweeps = state.counters.sweeps;
		static_cast<void>(job.ticket.Finish(Actuator::ActuatorStatus::done, report));
		job.active = false;
		state.sweepsActive--;
	}
}


// The receive handler of the stacks: the requests layer takes the packets of the command set, so this gets only the payloads it does not take.
void OnPayload(const VUB::Meta&, std::span<const uint8> payload) {
	(void)payload;
	state.unclaimed++;
}


using DeviceHandlers = VUB::Handlers<
	VUB::On<"actuator.set", &OnSet>,
	VUB::On<"actuator.sweep", &OnSweep>,
	VUB::On<"actuator.kick", &OnKick>,
	VUB::On<"actuator.nudge", &OnNudge>,
	VUB::On<"actuator.hang", &OnHang>,
	VUB::On<"actuator.counters", &OnCounters>>;


// The chain of the device's UART: the requests over Reliable, CRC-16/MODBUS, then COBS. Four tickets; the payload is 32 bytes, the
// largest packet of the command set.
template<typename LinkType>
struct UartActuatorStack {
	using Type = VUB::Stack<+[]() {
		return VUB::Builder()
			.Layer<VUB::Requests<Actuator::ActuatorCommands, DeviceHandlers>>().Tickets(4)
			.Layer<VUB::Reliable>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(32)
			.Transform<VUB::Crc16Modbus>()
			.Framing<VUB::Cobs>()
			.template Link<LinkType>()
			.MaxPayload(32)
			.template OnReceive<&OnPayload>()
			.template Clock<&ActuatorClockUs>();
	}>;
};


// The chain of the device's CDC-ACM serial port: the requests over Reliable, then COBS.
template<typename LinkType>
struct CdcActuatorStack {
	using Type = VUB::Stack<+[]() {
		return VUB::Builder()
			.Layer<VUB::Requests<Actuator::ActuatorCommands, DeviceHandlers>>().Tickets(4)
			.Layer<VUB::Reliable>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(32)
			.Framing<VUB::Cobs>()
			.template Link<LinkType>()
			.MaxPayload(32)
			.template OnReceive<&OnPayload>()
			.template Clock<&ActuatorClockUs>();
	}>;
};

}
