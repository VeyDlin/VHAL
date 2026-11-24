#include "SimCheck.h"
#include "RequestsMotor.h"
#include <Drivers/Interface/VUB/Requests/Commands.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <string_view>
#include <type_traits>


// The command description: the motor set of the requests design (section 2.1), and a second set that uses the options the motor set
// does not (Fire, NoReply with FailWhileDown, a delivered event, a command with a request and a status-only reply).


namespace Probe {

using ExtraCommands = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<"kick", 0x01>()
			.Fire()
		.Command<"clear", 0x02>()
			.NoReply()
			.FailWhileDown()
		.Command<"lamp.set", 0x03>()
			.Request<Motor::MotorFault>()
			.Reply<Motor::MotorStatus>()
			.ReplyTimeoutMs(100)
			.FailWhileDown()
		.Event<"tick", 0x20>()
			.Body<VUB::NoBody>()
			.Delivered();
}>;


using EmptyCommands = VUB::CommandSet<+[]() {
	return VUB::Commands();
}>;

}


// The motor set: the sizes of the bodies, the types and the flags of each command, and the lookups.
static_assert(Motor::MotorCommands::commandCount == 3);
static_assert(Motor::MotorCommands::eventCount == 1);
static_assert(Motor::MotorCommands::largestRequest == 5);
static_assert(Motor::MotorCommands::largestReply == 4);
static_assert(Motor::MotorCommands::largestEvent == 1);

static_assert(std::is_same_v<VUB::CommandOf<Motor::MotorCommands, "motor.start">::BodyType, Motor::MotorStart>);
static_assert(std::is_same_v<VUB::CommandOf<Motor::MotorCommands, "motor.start">::ReplyType, Motor::MotorReport>);
static_assert(std::is_same_v<VUB::CommandOf<Motor::MotorCommands, "motor.start">::StatusType, Motor::MotorStatus>);
static_assert(VUB::CommandOf<Motor::MotorCommands, "motor.start">::id == 0x12);
static_assert(std::is_same_v<VUB::CommandOf<Motor::MotorCommands, "motor.stop">::BodyType, VUB::NoBody>);
static_assert(std::is_same_v<VUB::CommandOf<Motor::MotorCommands, "motor.stop">::ReplyType, VUB::NoBody>);
static_assert(std::is_same_v<VUB::CommandOf<Motor::MotorCommands, "motor.reset">::StatusType, VUB::NoStatus>);
static_assert(VUB::CommandOf<Motor::MotorCommands, "motor.reset">::id == 0x14);
static_assert(std::is_same_v<VUB::EventOf<Motor::MotorCommands, "motor.fault">::BodyType, Motor::MotorFault>);
static_assert(VUB::EventOf<Motor::MotorCommands, "motor.fault">::id == 0x40);

static_assert(VUB::CommandInfoOf<Motor::MotorCommands, "motor.start">().level == 4);
static_assert(VUB::CommandInfoOf<Motor::MotorCommands, "motor.start">().interim);
static_assert(VUB::CommandInfoOf<Motor::MotorCommands, "motor.start">().cancellable);
static_assert(VUB::CommandInfoOf<Motor::MotorCommands, "motor.start">().replyTimeoutMs == 5000);
static_assert(VUB::CommandInfoOf<Motor::MotorCommands, "motor.start">().ticketTimeoutMs == 60000);
static_assert(!VUB::CommandInfoOf<Motor::MotorCommands, "motor.start">().failWhileDown);
static_assert(VUB::CommandInfoOf<Motor::MotorCommands, "motor.start">().request->maxSize == 5);
static_assert(VUB::CommandInfoOf<Motor::MotorCommands, "motor.start">().reply->maxSize == 4);
static_assert(VUB::CommandInfoOf<Motor::MotorCommands, "motor.stop">().level == 3);
static_assert(!VUB::CommandInfoOf<Motor::MotorCommands, "motor.stop">().cancellable);
static_assert(VUB::CommandInfoOf<Motor::MotorCommands, "motor.stop">().ticketTimeoutMs == 2000);
static_assert(VUB::CommandInfoOf<Motor::MotorCommands, "motor.reset">().level == 2);
static_assert(VUB::EventInfoOf<Motor::MotorCommands, "motor.fault">().body->maxSize == 1);
static_assert(!VUB::EventInfoOf<Motor::MotorCommands, "motor.fault">().delivered);

// The second set: Fire is level 1, NoReply with FailWhileDown is level 2, a delivered event, and the request of a reply.
static_assert(Probe::ExtraCommands::commandCount == 3);
static_assert(Probe::ExtraCommands::eventCount == 1);
static_assert(VUB::CommandInfoOf<Probe::ExtraCommands, "kick">().level == 1);
static_assert(VUB::CommandInfoOf<Probe::ExtraCommands, "clear">().level == 2);
static_assert(VUB::CommandInfoOf<Probe::ExtraCommands, "clear">().failWhileDown);
static_assert(VUB::CommandInfoOf<Probe::ExtraCommands, "lamp.set">().level == 3);
static_assert(VUB::CommandInfoOf<Probe::ExtraCommands, "lamp.set">().failWhileDown);
static_assert(VUB::CommandInfoOf<Probe::ExtraCommands, "lamp.set">().replyTimeoutMs == 100);
static_assert(std::is_same_v<VUB::CommandOf<Probe::ExtraCommands, "lamp.set">::BodyType, Motor::MotorFault>);
static_assert(std::is_same_v<VUB::CommandOf<Probe::ExtraCommands, "kick">::BodyType, VUB::NoBody>);
static_assert(std::is_same_v<VUB::CommandOf<Probe::ExtraCommands, "kick">::StatusType, VUB::NoStatus>);
static_assert(VUB::EventInfoOf<Probe::ExtraCommands, "tick">().delivered);
static_assert(VUB::EventInfoOf<Probe::ExtraCommands, "tick">().body->maxSize == 0);

// An empty set has no commands and no events, and no sizes.
static_assert(Probe::EmptyCommands::commandCount == 0);
static_assert(Probe::EmptyCommands::eventCount == 0);
static_assert(Probe::EmptyCommands::largestRequest == 0);


// The lookups by id: the layer's dispatch walks the tables, and finds a command or event by its id.
void TestLookupById() {
	const VUB::CommandInfo* start = Motor::MotorCommands::CommandById(0x12);
	SIM_EXPECT(start != nullptr);
	SIM_EXPECT(start->name == "motor.start");
	SIM_EXPECT(start->level == 4);
	SIM_EXPECT(Motor::MotorCommands::CommandById(0x13)->name == "motor.stop");
	SIM_EXPECT(Motor::MotorCommands::CommandById(0x77) == nullptr);
	SIM_EXPECT(Motor::MotorCommands::CommandById(0x40) == nullptr);
	SIM_EXPECT(Motor::MotorCommands::EventById(0x40)->name == "motor.fault");
	SIM_EXPECT(Motor::MotorCommands::EventById(0x12) == nullptr);
}


// The tables are in declaration order, so the layer can walk them.
void TestTablesAreInDeclarationOrder() {
	const std::array<VUB::CommandInfo, 3>& table = Motor::MotorCommands::commandTable;
	SIM_EXPECT(table.size() == 3);
	SIM_EXPECT(table[0].name == "motor.start");
	SIM_EXPECT(table[1].name == "motor.stop");
	SIM_EXPECT(table[2].name == "motor.reset");
	SIM_EXPECT(table[2].id == 0x14);
	SIM_EXPECT(table[2].replyTimeoutMs == 0);
}


// The view of a request body is the codec's view: the minimum and maximum sizes come with it.
void TestRequestViewsAreTheCodecViews() {
	// CommandInfoOf is a constant-expression lookup (it refuses a name the set lacks), so it is evaluated in a constant here.
	constexpr VUB::CommandInfo start = VUB::CommandInfoOf<Motor::MotorCommands, "motor.start">();
	const VUB::BodyView* request = start.request;
	SIM_EXPECT(request == &VUB::BodyViewOf<Motor::MotorStart>);
	SIM_EXPECT(request->minSize == 3);
	SIM_EXPECT(request->maxSize == 5);
}


int main() {
	TestLookupById();
	TestTablesAreInDeclarationOrder();
	TestRequestViewsAreTheCodecViews();
	return SimFinish("TestVubRequestsCommands");
}
