#pragma once
#include <Drivers/Interface/VUB/Requests/Commands.h>
#include <Utilities/DataTypes.h>


// The motor description of the requests design (section 2.1), shared by the requests tests. The bodies have their layouts here.


namespace Motor {

enum class MotorStatus : uint8 {
	started = 0,
	warming = 1,
	failed = 2
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
		.Command<"motor.start", 0x12>()
			.Request<MotorStart>()
			.Reply<MotorStatus, MotorReport>()
			.Interim()
			.Cancellable()
			.ReplyTimeoutMs(5000)
			.TicketTimeoutMs(60000)
		.Command<"motor.stop", 0x13>()
			.Reply<MotorStatus>()
			.ReplyTimeoutMs(2000)
			.TicketTimeoutMs(2000)
		.Command<"motor.reset", 0x14>()
			.NoReply()
		.Event<"motor.fault", 0x40>()
			.Body<MotorFault>();
}>;

}
