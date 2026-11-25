// Configuration of the built-in requests (ping and health) for the size table, over ReliableOf<40, 4> on CRC-16/MODBUS, COBS and a stream
// link. The flags pick the layer's role, the health Steps, the ping Steps and the counters the health reply needs:
//   SIZE_LAYER   0: no requests layer; 1: served command (Tickets(1), Refusals(2)); 2: requester (Open(2)); 3: both.
//   SIZE_HEALTH  bit 1: .Health<SizeHealth>() (asks); bit 2: .AnswerHealth<SizeHealth, &SizeFillHealth>() (answers).
//   SIZE_PING    1: .Ping(16) and .AnswerPing(16).
//   SIZE_COUNTED 1: Counted() on the layer and on Reliable, and HighWater() on the stack.
#include "SizeWire.h"
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/Layer/Reliable.h>
#include <Drivers/Interface/VUB/Requests.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Drivers/Interface/VUB/Transform/Crc.h>
#include <Utilities/Checksum/CRC/Preset/Crc16Modbus.h>


#ifndef SIZE_LAYER
#define SIZE_LAYER 0
#endif

#ifndef SIZE_HEALTH
#define SIZE_HEALTH 0
#endif

#ifndef SIZE_PING
#define SIZE_PING 0
#endif

#ifndef SIZE_COUNTED
#define SIZE_COUNTED 0
#endif


enum class SizeStatus : uint8 {
	done = 0
};


struct SizeBody {
	uint16 value = 0;
};


struct SizeHealth {
	uint32 uptimeS = 0;
	uint8 resetReason = 0;
	uint8 state = 0;
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


template<>
struct BodyOf<SizeHealth> {
	static consteval auto Layout() {
		return Fields<SizeHealth>()
			.Field<&SizeHealth::uptimeS>()
			.Field<&SizeHealth::resetReason>()
			.Field<&SizeHealth::state>();
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


void OnSizeHealth(const VUB::HealthReport<SizeHealth>& report) {
	sizeReceived = sizeReceived + static_cast<uint32>(report.result);
}


void OnSizePing(ResultStatus result, uint32 roundTripUs) {
	sizeReceived = sizeReceived + static_cast<uint32>(result) + roundTripUs;
}


void SizeFillHealth(SizeHealth& body) {
	body.uptimeS = SizeClock();
	body.resetReason = 1;
	body.state = 2;
}


#if SIZE_LAYER == 1 || SIZE_LAYER == 3
using SizeHandlers = VUB::Handlers<
	VUB::On<SizeCommand::one, &OnServeOne>>;
#else
using SizeHandlers = VUB::Handlers<>;
#endif


#if SIZE_LAYER == 1 || SIZE_LAYER == 3 || SIZE_PING || (SIZE_HEALTH & 2)
#define SIZE_TICKETS .Tickets(1).Refusals(2)
#else
#define SIZE_TICKETS
#endif

#if SIZE_LAYER >= 2 || SIZE_PING || (SIZE_HEALTH & 1)
#define SIZE_OPEN .Open(2)
#else
#define SIZE_OPEN
#endif

#if SIZE_COUNTED
#define SIZE_COUNT .Counted()
#else
#define SIZE_COUNT
#endif

#if SIZE_PING
#define SIZE_PING_STEPS .Ping(16).AnswerPing(16)
#else
#define SIZE_PING_STEPS
#endif

#if SIZE_HEALTH & 1
#define SIZE_ASK .Health<SizeHealth>()
#else
#define SIZE_ASK
#endif

#if SIZE_HEALTH & 2
#define SIZE_ANSWER .AnswerHealth<SizeHealth, &SizeFillHealth>()
#else
#define SIZE_ANSWER
#endif

#if SIZE_LAYER == 0
#define SIZE_UPPER
#else
#define SIZE_UPPER .Layer<VUB::Requests<SizeCommands, SizeHandlers>>() SIZE_OPEN SIZE_TICKETS SIZE_COUNT SIZE_PING_STEPS SIZE_ASK SIZE_ANSWER
#endif

#if SIZE_COUNTED
#define SIZE_RELIABLE_COUNT .Counted()
#define SIZE_HIGH_WATER .HighWater()
#else
#define SIZE_RELIABLE_COUNT
#define SIZE_HIGH_WATER
#endif


using SizeStack = VUB::Stack<+[]() {
	return VUB::Builder()
		SIZE_UPPER
		.Layer<VUB::ReliableOf<40, 4>>() SIZE_RELIABLE_COUNT .Retries(3).TimeoutMs(200).Window(4)
		.Transform<VUB::Crc<Checksum::Crc16Modbus>>().Framing<VUB::Cobs>().Link<SizeStreamLink>()
		SIZE_HIGH_WATER
		.MaxPayload(40)
		.OnReceive<&SizeReceive>()
		.Clock<&SizeClock>();
}>;


SizeStreamLink sizeLink;
SizeStack sizeStack(sizeLink);


void SizeRun() {
#if SIZE_LAYER != 0 && ((SIZE_HEALTH & 1) || SIZE_PING)
	auto handle = sizeStack.Requests();
#if SIZE_HEALTH & 1
	(void)handle.template Health<&OnSizeHealth>();
#endif
#if SIZE_PING
	(void)handle.template Ping<&OnSizePing>(0);
#endif
#endif
	sizeStack.Find<VUB::ReliableOf<40, 4>>().SetSession(0x1234);
	SizeRunReliable(sizeStack);
}
