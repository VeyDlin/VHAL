#include "SimCheck.h"
#include "RequestsMotor.h"
#include <Drivers/Interface/VUB/Requests/Commands.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <type_traits>


// The command description: the motor set, and a second set that uses the options the motor set does not (Fire, NoReply with
// FailWhileDown, a delivered event, a command with a request and a status-only reply). The ids are values of enum classes.


namespace Probe {

enum class ExtraCommand : uint8 {
	kick = 0x01,
	clear = 0x02,
	lampSet = 0x03
};

enum class ExtraEvent : uint8 {
	tick = 0x20
};

using ExtraCommands = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<ExtraCommand::kick>()
			.Fire()
		.Command<ExtraCommand::clear>()
			.NoReply()
			.FailWhileDown()
		.Command<ExtraCommand::lampSet>()
			.Request<Motor::MotorFault>()
			.Reply<Motor::MotorStatus>()
			.ReplyTimeoutMs(100)
			.FailWhileDown()
		.Event<ExtraEvent::tick>()
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

static_assert(std::is_same_v<VUB::CommandOf<Motor::MotorCommands, Motor::MotorCommand::start>::BodyType, Motor::MotorStart>);
static_assert(std::is_same_v<VUB::CommandOf<Motor::MotorCommands, Motor::MotorCommand::start>::ReplyType, Motor::MotorReport>);
static_assert(std::is_same_v<VUB::CommandOf<Motor::MotorCommands, Motor::MotorCommand::start>::StatusType, Motor::MotorStatus>);
static_assert(VUB::CommandOf<Motor::MotorCommands, Motor::MotorCommand::start>::id == 0x12);
static_assert(std::is_same_v<VUB::CommandOf<Motor::MotorCommands, Motor::MotorCommand::stop>::BodyType, VUB::NoBody>);
static_assert(std::is_same_v<VUB::CommandOf<Motor::MotorCommands, Motor::MotorCommand::stop>::ReplyType, VUB::NoBody>);
static_assert(std::is_same_v<VUB::CommandOf<Motor::MotorCommands, Motor::MotorCommand::reset>::StatusType, VUB::NoStatus>);
static_assert(VUB::CommandOf<Motor::MotorCommands, Motor::MotorCommand::reset>::id == 0x14);
static_assert(std::is_same_v<VUB::EventOf<Motor::MotorCommands, Motor::MotorEvent::fault>::BodyType, Motor::MotorFault>);
static_assert(VUB::EventOf<Motor::MotorCommands, Motor::MotorEvent::fault>::id == 0x40);

static_assert(VUB::CommandInfoOf<Motor::MotorCommands, Motor::MotorCommand::start>().level == 4);
static_assert(VUB::CommandInfoOf<Motor::MotorCommands, Motor::MotorCommand::start>().interim);
static_assert(VUB::CommandInfoOf<Motor::MotorCommands, Motor::MotorCommand::start>().cancellable);
static_assert(VUB::CommandInfoOf<Motor::MotorCommands, Motor::MotorCommand::start>().replyTimeoutMs == 5000);
static_assert(VUB::CommandInfoOf<Motor::MotorCommands, Motor::MotorCommand::start>().ticketTimeoutMs == 60000);
static_assert(!VUB::CommandInfoOf<Motor::MotorCommands, Motor::MotorCommand::start>().failWhileDown);
static_assert(VUB::CommandInfoOf<Motor::MotorCommands, Motor::MotorCommand::start>().request->maxSize == 5);
static_assert(VUB::CommandInfoOf<Motor::MotorCommands, Motor::MotorCommand::start>().reply->maxSize == 4);
static_assert(VUB::CommandInfoOf<Motor::MotorCommands, Motor::MotorCommand::stop>().level == 3);
static_assert(!VUB::CommandInfoOf<Motor::MotorCommands, Motor::MotorCommand::stop>().cancellable);
static_assert(VUB::CommandInfoOf<Motor::MotorCommands, Motor::MotorCommand::stop>().ticketTimeoutMs == 2000);
static_assert(VUB::CommandInfoOf<Motor::MotorCommands, Motor::MotorCommand::reset>().level == 2);
static_assert(VUB::EventInfoOf<Motor::MotorCommands, Motor::MotorEvent::fault>().body->maxSize == 1);
static_assert(!VUB::EventInfoOf<Motor::MotorCommands, Motor::MotorEvent::fault>().delivered);

// The second set: Fire is level 1, NoReply with FailWhileDown is level 2, a delivered event, and the request of a reply.
static_assert(Probe::ExtraCommands::commandCount == 3);
static_assert(Probe::ExtraCommands::eventCount == 1);
static_assert(VUB::CommandInfoOf<Probe::ExtraCommands, Probe::ExtraCommand::kick>().level == 1);
static_assert(VUB::CommandInfoOf<Probe::ExtraCommands, Probe::ExtraCommand::clear>().level == 2);
static_assert(VUB::CommandInfoOf<Probe::ExtraCommands, Probe::ExtraCommand::clear>().failWhileDown);
static_assert(VUB::CommandInfoOf<Probe::ExtraCommands, Probe::ExtraCommand::lampSet>().level == 3);
static_assert(VUB::CommandInfoOf<Probe::ExtraCommands, Probe::ExtraCommand::lampSet>().failWhileDown);
static_assert(VUB::CommandInfoOf<Probe::ExtraCommands, Probe::ExtraCommand::lampSet>().replyTimeoutMs == 100);
static_assert(std::is_same_v<VUB::CommandOf<Probe::ExtraCommands, Probe::ExtraCommand::lampSet>::BodyType, Motor::MotorFault>);
static_assert(std::is_same_v<VUB::CommandOf<Probe::ExtraCommands, Probe::ExtraCommand::kick>::BodyType, VUB::NoBody>);
static_assert(std::is_same_v<VUB::CommandOf<Probe::ExtraCommands, Probe::ExtraCommand::kick>::StatusType, VUB::NoStatus>);
static_assert(VUB::EventInfoOf<Probe::ExtraCommands, Probe::ExtraEvent::tick>().delivered);
static_assert(VUB::EventInfoOf<Probe::ExtraCommands, Probe::ExtraEvent::tick>().body->maxSize == 0);

// An empty set has no commands and no events, and no sizes.
static_assert(Probe::EmptyCommands::commandCount == 0);
static_assert(Probe::EmptyCommands::eventCount == 0);
static_assert(Probe::EmptyCommands::largestRequest == 0);


// The lookups by the byte id: the layer's dispatch walks the tables, and finds a command or event by its id.
void TestLookupById() {
	const VUB::CommandInfo* start = Motor::MotorCommands::CommandById(0x12);
	SIM_EXPECT(start != nullptr);
	SIM_EXPECT(start->id == 0x12);
	SIM_EXPECT(start->level == 4);
	SIM_EXPECT(Motor::MotorCommands::CommandById(0x13)->id == 0x13);
	SIM_EXPECT(Motor::MotorCommands::CommandById(0x77) == nullptr);
	SIM_EXPECT(Motor::MotorCommands::CommandById(0x40) == nullptr);
	SIM_EXPECT(Motor::MotorCommands::EventById(0x40)->id == 0x40);
	SIM_EXPECT(Motor::MotorCommands::EventById(0x12) == nullptr);
}


// The tables are in declaration order, so the layer can walk them.
void TestTablesAreInDeclarationOrder() {
	const std::array<VUB::CommandInfo, 3>& table = Motor::MotorCommands::commandTable;
	SIM_EXPECT(table.size() == 3);
	SIM_EXPECT(table[0].id == 0x12);
	SIM_EXPECT(table[1].id == 0x13);
	SIM_EXPECT(table[2].id == 0x14);
	SIM_EXPECT(table[2].replyTimeoutMs == 0);
}


// The view of a request body is the codec's view: the minimum and maximum sizes come with it.
void TestRequestViewsAreTheCodecViews() {
	// CommandInfoOf is a constant-expression lookup (it refuses an id the set lacks), so it is evaluated in a constant here.
	constexpr VUB::CommandInfo start = VUB::CommandInfoOf<Motor::MotorCommands, Motor::MotorCommand::start>();
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
