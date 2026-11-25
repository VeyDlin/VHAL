#include "SimCheck.h"
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/Layer/Reliable.h>
#include <Drivers/Interface/VUB/Requests.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Drivers/Interface/VUB/Transform/Crc.h>
#include <Utilities/Checksum/CRC/Preset/Crc16Modbus.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <cstddef>
#include <span>

// The first example of Layer/Requests.md, compiled as it is written there, with the main of this test added below it.

// --- snippet: Layer/Requests.md, the device ---
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/Layer/Reliable.h>
#include <Drivers/Interface/VUB/Requests.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Drivers/Interface/VUB/Transform/Crc.h>
#include <Utilities/Checksum/CRC/Preset/Crc16Modbus.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <span>


namespace Motor {

enum class MotorStatus : uint8 {
	started = 0,
	warming = 1
};


enum class MotorCommand : uint8 {
	start = 0x12,
	stop = 0x13
};


struct MotorStart {
	uint16 speedRpm = 0;
	uint8 direction = 0;
};


struct MotorReport {
	uint16 speedRpm = 0;
};

}


namespace VUB {

template<>
struct BodyOf<Motor::MotorStart> {
	static consteval auto Layout() {
		return Fields<Motor::MotorStart>()
			.Field<&Motor::MotorStart::speedRpm>()
			.Field<&Motor::MotorStart::direction>();
	}
};


template<>
struct BodyOf<Motor::MotorReport> {
	static consteval auto Layout() {
		return Fields<Motor::MotorReport>()
			.Field<&Motor::MotorReport::speedRpm>();
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
			.ReplyTimeoutMs(5000)
			.TicketTimeoutMs(60000)
		.Command<MotorCommand::stop>()
			.Reply<MotorStatus>()
			.ReplyTimeoutMs(2000)
			.TicketTimeoutMs(2000);
}>;


using StartTicket = VUB::Ticket<MotorCommands, MotorCommand::start>;
using StopTicket = VUB::Ticket<MotorCommands, MotorCommand::stop>;


void OnStart(StartTicket ticket, const MotorStart& body) {
	(void)body;
	(void)ticket.Interim(MotorStatus::warming);
	MotorReport report{};
	(void)ticket.Finish(MotorStatus::started, report);
}


void OnStop(StopTicket ticket) {
	(void)ticket.Finish(MotorStatus::started);
}


using DeviceHandlers = VUB::Handlers<
	VUB::On<MotorCommand::start, &OnStart>,
	VUB::On<MotorCommand::stop, &OnStop>>;

}


// The link is a stub here: a board puts its UART in its place. Write takes the bytes and reports them sent.
struct UartLink {
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().MaxPacket(64).HalfDuplex().Starts(VUB::Initiator::Either);


	Result<std::size_t> Write(std::span<const uint8> bytes) {
		return bytes.size();
	}
};


void HandlePacket(const VUB::Meta&, std::span<const uint8>) {
}


uint32 MicrosecondClock() {
	return 0;
}


using DeviceLink = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<Motor::MotorCommands, Motor::DeviceHandlers>>()
			.Tickets(4)
		.Layer<VUB::ReliableOf<40, 4>>()
			.Retries(3)
			.TimeoutMs(200)
			.Window(4)
		.Transform<VUB::Crc<Checksum::Crc16Modbus>>()
		.Framing<VUB::Cobs>()
		.Link<UartLink>()
		.MaxPayload(40)
		.OnReceive<&HandlePacket>()
		.Clock<&MicrosecondClock>();
}>;
// --- end snippet ---


// --- snippet: Layer/Requests.md, the requester ---
void OnStartFinal(const VUB::Final<Motor::MotorStatus, Motor::MotorReport>& reply) {
	(void)reply;
}


void OnStartInterim(const VUB::Progress<Motor::MotorStatus>& progress) {
	(void)progress;
}


void OnStopFinal(const VUB::Final<Motor::MotorStatus, VUB::NoBody>& reply) {
	(void)reply;
}


using RequesterLink = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<Motor::MotorCommands, VUB::Handlers<>>>().Open(2)
		.Layer<VUB::ReliableOf<40, 4>>()
			.Retries(3)
			.TimeoutMs(200)
			.Window(4)
		.Transform<VUB::Crc<Checksum::Crc16Modbus>>()
		.Framing<VUB::Cobs>()
		.Link<UartLink>()
		.MaxPayload(40)
		.OnReceive<&HandlePacket>()
		.Clock<&MicrosecondClock>();
}>;


// A start request with an interim reply, and a stop request, which has no body and no interim replies.
inline void SendCommands(RequesterLink& link) {
	(void)link.Requests().Send<Motor::MotorCommand::start, &OnStartFinal, &OnStartInterim>(Motor::MotorStart{1200, 1});
	(void)link.Requests().Send<Motor::MotorCommand::stop, &OnStopFinal>();
}
// --- end snippet ---


int main() {
	static_assert(sizeof(DeviceLink) > 0);
	static_assert(sizeof(RequesterLink) > 0);
	SIM_EXPECT(Motor::MotorCommands::commandCount == 2);
	SIM_EXPECT(Motor::MotorCommands::largestRequest == 3);
	return SimFinish("TestVubRequestsLayerWiki");
}
