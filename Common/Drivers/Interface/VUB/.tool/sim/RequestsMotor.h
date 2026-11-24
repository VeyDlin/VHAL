#pragma once
#include <Drivers/Interface/VUB/Requests/Commands.h>
#include <Utilities/DataTypes.h>


// The motor description shared by the requests tests. The ids are values of enum classes with a uint8 underlying type; the value is the
// id on the wire. The bodies have their layouts here.


namespace Motor {

enum class MotorStatus : uint8 {
	started = 0,
	warming = 1,
	failed = 2
};

enum class MotorCommand : uint8 {
	start = 0x12,
	stop = 0x13,
	reset = 0x14
};

enum class MotorEvent : uint8 {
	fault = 0x40
};

struct MotorStart {
	uint16 speedRpm = 0;
	uint8 direction = 0;
	uint16 rampMs = 300;
};

struct MotorReport {
	uint16 speedRpm = 0;
	uint16 currentMa = 0;
};

struct MotorFault {
	uint8 code = 0;
};

struct MotorHealth {
	uint32 uptimeS = 0;
	uint8 resetReason = 0;
	uint8 state = 0;
};

}


namespace VUB {

template<>
struct BodyOf<Motor::MotorStart> {
	static consteval auto Layout() {
		return Fields<Motor::MotorStart>()
			.Field<&Motor::MotorStart::speedRpm>()
			.Field<&Motor::MotorStart::direction>()
			.Optional<&Motor::MotorStart::rampMs>();
	}
};


template<>
struct BodyOf<Motor::MotorReport> {
	static consteval auto Layout() {
		return Fields<Motor::MotorReport>()
			.Field<&Motor::MotorReport::speedRpm>()
			.Field<&Motor::MotorReport::currentMa>();
	}
};


template<>
struct BodyOf<Motor::MotorFault> {
	static consteval auto Layout() {
		return Fields<Motor::MotorFault>()
			.Field<&Motor::MotorFault::code>();
	}
};


template<>
struct BodyOf<Motor::MotorHealth> {
	static consteval auto Layout() {
		return Fields<Motor::MotorHealth>()
			.Field<&Motor::MotorHealth::uptimeS>()
			.Field<&Motor::MotorHealth::resetReason>()
			.Field<&Motor::MotorHealth::state>();
	}
};

}


namespace Motor {

using MotorCommands = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<MotorCommand::start>()
			.Request<MotorStart>()
			.Reply<MotorStatus, MotorReport>()
			.Interim()
			.Cancellable()
			.ReplyTimeoutMs(5000)
			.TicketTimeoutMs(60000)
		.Command<MotorCommand::stop>()
			.Reply<MotorStatus>()
			.ReplyTimeoutMs(2000)
			.TicketTimeoutMs(2000)
		.Command<MotorCommand::reset>()
			.NoReply()
		.Event<MotorEvent::fault>()
			.Body<MotorFault>();
}>;

}
