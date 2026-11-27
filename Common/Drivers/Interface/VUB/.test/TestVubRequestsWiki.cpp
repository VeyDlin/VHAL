#include "SimCheck.h"
#include <Drivers/Interface/VUB/Requests/Body.h>
#include <Drivers/Interface/VUB/Requests/Commands.h>
#include <Drivers/Interface/VUB/Requests/Handlers.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>
#include <type_traits>


// The snippets of the requests wiki pages (Body.md and Commands.md), as they are written there. They are compiled and checked here.
// The ticket of a handler is the layer's class; this file gives it an empty stub, so that the handler lists can name it.


namespace VUB {

template<typename Commands, auto Id>
class Ticket {
};

}


// --- snippet: Body.md, the layout ---
// The request body of the motor start command: a speed, a direction, and a ramp that version 1 peers do not send.
namespace Motor {

struct MotorStart {
	uint16 speedRpm = 0;
	uint8 direction = 0;
	uint16 rampMs = 300;
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

}
// --- end snippet ---


// --- snippet: Body.md, encode and decode ---
// Encodes MotorStart{1200, 1} and decodes the version one form, which has no ramp.
void EncodeAndDecode() {
	Motor::MotorStart start{};
	start.speedRpm = 1200;
	start.direction = 1;

	std::array<uint8, 16> bytes{};
	Result<std::size_t> written = VUB::Encode(start, std::span<uint8>(bytes));
	if (written.IsOk()) {
		// written.Value() is 5, and bytes holds B0 04 01 2C 01.
	}

	const std::array<uint8, 3> versionOne = {0xB0, 0x04, 0x01};
	Motor::MotorStart received{};
	if (VUB::Decode(std::span<const uint8>(versionOne), received) == ResultStatus::ok) {
		// received.rampMs is 300: the version one form has no ramp field.
	}
}
// --- end snippet ---


// --- snippet: Commands.md, the motor description ---
namespace Motor {

enum class MotorStatus : uint8 {
	started = 0,
	warming = 1,
	failed = 2
};

struct MotorReport {
	uint16 speedRpm = 0;
	uint16 currentMa = 0;
};

struct MotorFault {
	uint8 code = 0;
};

enum class MotorCommand : uint8 {
	start = 0x12,
	stop = 0x13,
	reset = 0x14
};

enum class MotorEvent : uint8 {
	fault = 0x40
};

}


namespace VUB {

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

}


// The motor description, shared by the device and the PC.
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
// --- end snippet ---


// --- snippet: Commands.md, the lookups ---
static_assert(VUB::CommandOf<Motor::MotorCommands, Motor::MotorCommand::start>::id == 0x12);
static_assert(VUB::CommandInfoOf<Motor::MotorCommands, Motor::MotorCommand::start>().level == 4);
static_assert(Motor::MotorCommands::largestRequest == 5);

// The lookup of a received id: CommandById returns the entry of the command with this id, and the layer reads its body with the view.
const VUB::CommandInfo* LookUpStart() {
	return Motor::MotorCommands::CommandById(0x12);
}
// --- end snippet ---


// --- snippet: Commands.md, the handler lists ---
namespace Motor {

using StartTicket = VUB::Ticket<MotorCommands, MotorCommand::start>;
using StopTicket = VUB::Ticket<MotorCommands, MotorCommand::stop>;

void OnStart(StartTicket ticket, const MotorStart& body) {
	(void)ticket;
	(void)body;
}

void OnStartCancel(StartTicket ticket) {
	(void)ticket;
}

void OnStop(StopTicket ticket) {
	(void)ticket;
}

void OnReset() {
}

void OnFault(const MotorFault& fault) {
	(void)fault;
}

using DeviceHandlers = VUB::Handlers<
	VUB::On<MotorCommand::start, &OnStart>,
	VUB::OnCancel<MotorCommand::start, &OnStartCancel>,
	VUB::On<MotorCommand::stop, &OnStop>,
	VUB::On<MotorCommand::reset, &OnReset>>;

using PcHandlers = VUB::Handlers<
	VUB::OnEvent<MotorEvent::fault, &OnFault>>;

}


static_assert(VUB::CheckHandlers<Motor::MotorCommands, Motor::DeviceHandlers>());
static_assert(VUB::CheckHandlers<Motor::MotorCommands, Motor::PcHandlers>());
// --- end snippet ---


int main() {
	EncodeAndDecode();
	SIM_EXPECT(LookUpStart() != nullptr);
	SIM_EXPECT(LookUpStart()->id == 0x12);
	SIM_EXPECT(VUB::BodyViewOf<Motor::MotorStart>.maxSize == 5);
	return SimFinish("TestVubRequestsWiki");
}
