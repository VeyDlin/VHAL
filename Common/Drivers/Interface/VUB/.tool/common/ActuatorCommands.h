#pragma once
#include <Drivers/Interface/VUB/Requests.h>
#include <Utilities/DataTypes.h>


// The command set of the requests demo: a simulated actuator with a counter. The device (.demo/STM32G4/Application/DemoVub) and the PC
// check (vubcheck --requests) both compile this header, so the ids, the bodies and the reply layouts are described once.
//
// Each request body starts with a token the PC chooses, and each reply that carries the body echoes the token, so the PC can compare
// the bodies. The statuses of the interims and finals are one enum: step1 to step8 are the interims of a sweep, in order, and done is
// the final of a sweep.


namespace Actuator {

enum class ActuatorStatus : uint8 {
	applied = 0,
	done = 1,
	step1 = 2,
	step2,
	step3,
	step4,
	step5,
	step6,
	step7,
	step8
};


// The largest number of steps of a sweep, and the largest time of a step in milliseconds.
constexpr uint8 MaxSweepSteps = 8;
constexpr uint16 MaxStepMs = 1000;


struct SetBody {
	uint32 token = 0;
	uint16 level = 0;
	uint8 mode = 0;
};


struct SetReport {
	uint32 token = 0;
	uint16 level = 0;
	uint32 sets = 0;
};


struct SweepBody {
	uint32 token = 0;
	uint8 steps = 0;
	uint16 stepMs = 0;
};


struct SweepReport {
	uint32 token = 0;
	uint8 steps = 0;
	uint32 sweeps = 0;
};


struct TokenBody {
	uint32 token = 0;
};


// The counters of the device: how many of each command ran. A hang counts the requests its handler took, not the time-outs.
struct Counters {
	uint32 sets = 0;
	uint32 sweeps = 0;
	uint32 kicks = 0;
	uint32 nudges = 0;
	uint32 hangs = 0;
};

}


namespace VUB {

template<>
struct BodyOf<Actuator::SetBody> {
	static consteval auto Layout() {
		return Fields<Actuator::SetBody>()
			.Field<&Actuator::SetBody::token>()
			.Field<&Actuator::SetBody::level>()
			.Field<&Actuator::SetBody::mode>();
	}
};


template<>
struct BodyOf<Actuator::SetReport> {
	static consteval auto Layout() {
		return Fields<Actuator::SetReport>()
			.Field<&Actuator::SetReport::token>()
			.Field<&Actuator::SetReport::level>()
			.Field<&Actuator::SetReport::sets>();
	}
};


template<>
struct BodyOf<Actuator::SweepBody> {
	static consteval auto Layout() {
		return Fields<Actuator::SweepBody>()
			.Field<&Actuator::SweepBody::token>()
			.Field<&Actuator::SweepBody::steps>()
			.Field<&Actuator::SweepBody::stepMs>();
	}
};


template<>
struct BodyOf<Actuator::SweepReport> {
	static consteval auto Layout() {
		return Fields<Actuator::SweepReport>()
			.Field<&Actuator::SweepReport::token>()
			.Field<&Actuator::SweepReport::steps>()
			.Field<&Actuator::SweepReport::sweeps>();
	}
};


template<>
struct BodyOf<Actuator::TokenBody> {
	static consteval auto Layout() {
		return Fields<Actuator::TokenBody>()
			.Field<&Actuator::TokenBody::token>();
	}
};


template<>
struct BodyOf<Actuator::Counters> {
	static consteval auto Layout() {
		return Fields<Actuator::Counters>()
			.Field<&Actuator::Counters::sets>()
			.Field<&Actuator::Counters::sweeps>()
			.Field<&Actuator::Counters::kicks>()
			.Field<&Actuator::Counters::nudges>()
			.Field<&Actuator::Counters::hangs>();
	}
};

}


namespace Actuator {

// The commands. The ids are the device's and the PC's. actuator.absent has no handler on the device, so the device refuses it as not
// supported; the PC uses it for that refusal. The description is a named function, not a lambda: a field of a type built on a lambda
// has internal linkage, and the ticket fields of the device's sweep jobs would warn about it.
constexpr auto DescribeActuator() {
	return VUB::Commands()
		.Command<"actuator.set", 0x12>()
			.Request<SetBody>()
			.Reply<ActuatorStatus, SetReport>()
			.ReplyTimeoutMs(1000)
			.TicketTimeoutMs(1000)
		.Command<"actuator.sweep", 0x13>()
			.Request<SweepBody>()
			.Reply<ActuatorStatus, SweepReport>()
			.Interim()
			.ReplyTimeoutMs(12000)
			.TicketTimeoutMs(2000)
		.Command<"actuator.kick", 0x14>()
			.Request<TokenBody>()
			.NoReply()
		.Command<"actuator.nudge", 0x15>()
			.Request<TokenBody>()
			.Fire()
		.Command<"actuator.hang", 0x16>()
			.Request<TokenBody>()
			.Reply<ActuatorStatus>()
			.ReplyTimeoutMs(2000)
			.TicketTimeoutMs(300)
		.Command<"actuator.counters", 0x17>()
			.Reply<ActuatorStatus, Counters>()
			.ReplyTimeoutMs(1000)
			.TicketTimeoutMs(1000)
		.Command<"actuator.absent", 0x7F>()
			.Reply<ActuatorStatus>()
			.ReplyTimeoutMs(1000);
}

using ActuatorCommands = VUB::CommandSet<&DescribeActuator>;

}
