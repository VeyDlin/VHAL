#include "SimCheck.h"
#include "RequestsMotor.h"
#include <Drivers/Interface/VUB/Requests/Handlers.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <type_traits>


// The handler lists of the motor set, checked against it. The ticket is the layer's class; this test gives it an empty stub, so that the
// handlers can be defined with empty bodies. The test never calls a handler: every function is named by its address at compile time.


namespace VUB {

template<typename Commands, auto Id>
class Ticket {
};

}


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

void FillHealth(MotorHealth& health) {
	(void)health;
}

using DeviceHandlers = VUB::Handlers<
	VUB::On<MotorCommand::start, &OnStart>,
	VUB::OnCancel<MotorCommand::start, &OnStartCancel>,
	VUB::On<MotorCommand::stop, &OnStop>,
	VUB::On<MotorCommand::reset, &OnReset>>;


void OnStartInterim(const VUB::Progress<MotorStatus>& progress) {
	(void)progress;
}

void OnStartFinal(const VUB::Final<MotorStatus, MotorReport>& reply) {
	(void)reply;
}

void OnStopFinal(const VUB::Final<MotorStatus, VUB::NoBody>& reply) {
	(void)reply;
}

void OnResetDelivered(ResultStatus result) {
	(void)result;
}

void OnFault(const MotorFault& fault) {
	(void)fault;
}

using PcHandlers = VUB::Handlers<
	VUB::OnEvent<MotorEvent::fault, &OnFault>>;

}


// The device and PC lists satisfy the checks of their kinds against the motor set. A wrong signature is a builder error snippet.
static_assert(VUB::CheckHandlers<Motor::MotorCommands, Motor::DeviceHandlers>());
static_assert(VUB::CheckHandlers<Motor::MotorCommands, Motor::PcHandlers>());

// The requester's finals and interims: Send's Final and Interim arguments are checked by the same functions.
static_assert(VUB::CheckFinal<Motor::MotorCommands, Motor::MotorCommand::start, &Motor::OnStartFinal>());
static_assert(VUB::CheckFinal<Motor::MotorCommands, Motor::MotorCommand::stop, &Motor::OnStopFinal>());
static_assert(VUB::CheckFinal<Motor::MotorCommands, Motor::MotorCommand::reset, &Motor::OnResetDelivered>());
static_assert(VUB::CheckProgress<Motor::MotorCommands, Motor::MotorCommand::start, &Motor::OnStartInterim>());


// The lookups by id and kind: the layer finds the handler of a command, and the handler's function is the one declared.
static_assert(VUB::HasHandler<Motor::DeviceHandlers, VUB::HandlerKind::on, Motor::MotorCommand::start>());
static_assert(VUB::HasHandler<Motor::DeviceHandlers, VUB::HandlerKind::onCancel, Motor::MotorCommand::start>());
static_assert(!VUB::HasHandler<Motor::DeviceHandlers, VUB::HandlerKind::onCancel, Motor::MotorCommand::stop>());
static_assert(!VUB::HasHandler<Motor::DeviceHandlers, VUB::HandlerKind::on, Motor::MotorEvent::fault>());
static_assert(VUB::HasHandler<Motor::PcHandlers, VUB::HandlerKind::onEvent, Motor::MotorEvent::fault>());
static_assert(VUB::HandlerEntryOf<Motor::DeviceHandlers, VUB::HandlerKind::on, Motor::MotorCommand::start>::Type::fn == &Motor::OnStart);
static_assert(VUB::HandlerEntryOf<Motor::DeviceHandlers, VUB::HandlerKind::onCancel, Motor::MotorCommand::start>::Type::fn == &Motor::OnStartCancel);
static_assert(VUB::HandlerEntryOf<Motor::DeviceHandlers, VUB::HandlerKind::on, Motor::MotorCommand::reset>::Type::fn == &Motor::OnReset);
static_assert(VUB::HandlerEntryOf<Motor::PcHandlers, VUB::HandlerKind::onEvent, Motor::MotorEvent::fault>::Type::fn == &Motor::OnFault);


int main() {
	return SimFinish("TestVubRequestsHandlers");
}
