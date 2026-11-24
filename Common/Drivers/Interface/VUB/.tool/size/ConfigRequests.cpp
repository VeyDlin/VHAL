// Configuration of the requests layer for the size table. The flags pick the chain under the layer, the role of the layer, and the number of
// commands with a Send site (size.sh gives them per row):
//   SIZE_BASE   1: Crc16Modbus, Cobs, stream link; 2: Cobs, stream link; 3: ReliableOf<40, 4> over Crc16Modbus, Cobs, stream link.
//   SIZE_ROLE   0: no layer; 1: responder only (one command, Tickets(1), Refusals(2)); 2: requester only (Open(2)); 3: both.
//   SIZE_SENDS  1 or 4: the number of commands with a Send site, in the requester role.
#include "SizeWire.h"
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/Layer/Reliable.h>
#include <Drivers/Interface/VUB/Requests.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Drivers/Interface/VUB/Transform/Crc.h>


#ifndef SIZE_BASE
#define SIZE_BASE 1
#endif

#ifndef SIZE_ROLE
#define SIZE_ROLE 3
#endif

#ifndef SIZE_SENDS
#define SIZE_SENDS 1
#endif


enum class SizeStatus : uint8 {
	done = 0
};


struct SizeBody {
	uint16 value = 0;
};


enum class SizeCommand : uint8 {
	one = 0x10,
	two = 0x11,
	three = 0x12,
	four = 0x13
};


namespace VUB {

template<>
struct BodyOf<SizeBody> {
	static consteval auto Layout() {
		return Fields<SizeBody>()
			.Field<&SizeBody::value>();
	}
};

}


using SizeCommands = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<SizeCommand::one>()
			.Request<SizeBody>()
			.Reply<SizeStatus>()
			.ReplyTimeoutMs(1000)
			.TicketTimeoutMs(1000)
		.Command<SizeCommand::two>()
			.Request<SizeBody>()
			.Reply<SizeStatus>()
			.ReplyTimeoutMs(1000)
			.TicketTimeoutMs(1000)
		.Command<SizeCommand::three>()
			.Request<SizeBody>()
			.Reply<SizeStatus>()
			.ReplyTimeoutMs(1000)
			.TicketTimeoutMs(1000)
		.Command<SizeCommand::four>()
			.Request<SizeBody>()
			.Reply<SizeStatus>()
			.ReplyTimeoutMs(1000)
			.TicketTimeoutMs(1000);
}>;


void OnServeOne(VUB::Ticket<SizeCommands, SizeCommand::one> ticket, const SizeBody& body) {
	(void)body;
	(void)ticket.Finish(SizeStatus::done);
}


void OnFinal(const VUB::Final<SizeStatus, VUB::NoBody>& reply) {
	sizeReceived = sizeReceived + static_cast<uint32>(reply.result);
}


#if SIZE_ROLE == 1 || SIZE_ROLE == 3
using SizeHandlers = VUB::Handlers<
	VUB::On<SizeCommand::one, &OnServeOne>>;
#else
using SizeHandlers = VUB::Handlers<>;
#endif


#if SIZE_ROLE == 1
#define SIZE_UPPER .Layer<VUB::Requests<SizeCommands, SizeHandlers>>().Tickets(1).Refusals(2)
#elif SIZE_ROLE == 2
#define SIZE_UPPER .Layer<VUB::Requests<SizeCommands, SizeHandlers>>().Open(2)
#elif SIZE_ROLE == 3
#define SIZE_UPPER .Layer<VUB::Requests<SizeCommands, SizeHandlers>>().Open(2).Tickets(1).Refusals(2)
#else
#define SIZE_UPPER
#endif


#if SIZE_BASE == 1
#define SIZE_LOWER .Transform<VUB::Crc16Modbus>().Framing<VUB::Cobs>().Link<SizeStreamLink>()
#elif SIZE_BASE == 2
#define SIZE_LOWER .Framing<VUB::Cobs>().Link<SizeStreamLink>()
#else
#define SIZE_LOWER .Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4).Transform<VUB::Crc16Modbus>().Framing<VUB::Cobs>().Link<SizeStreamLink>()
#endif


using SizeStack = VUB::Stack<+[]() {
	return VUB::Builder()
		SIZE_UPPER
		SIZE_LOWER
		.MaxPayload(40)
		.OnReceive<&SizeReceive>()
		.Clock<&SizeClock>();
}>;


SizeStreamLink sizeLink;
SizeStack sizeStack(sizeLink);


#if SIZE_ROLE == 2 || SIZE_ROLE == 3
template<typename HandleType>
void SendAll(HandleType& handle) {
	(void)handle.template Send<SizeCommand::one, &OnFinal>(SizeBody{});
#if SIZE_SENDS == 4
	(void)handle.template Send<SizeCommand::two, &OnFinal>(SizeBody{});
	(void)handle.template Send<SizeCommand::three, &OnFinal>(SizeBody{});
	(void)handle.template Send<SizeCommand::four, &OnFinal>(SizeBody{});
#endif
}
#endif


void SizeRun() {
#if SIZE_ROLE == 2 || SIZE_ROLE == 3
	auto handle = sizeStack.Requests();
#if SIZE_BASE != 3
	handle.SetSession(1);
#endif
	SendAll(handle);
#endif
#if SIZE_BASE == 3
	sizeStack.Find<VUB::ReliableOf<40, 4>>().SetSession(0x1234);
	SizeRunReliable(sizeStack);
#else
	SizeRunStream(sizeStack);
#endif
}
