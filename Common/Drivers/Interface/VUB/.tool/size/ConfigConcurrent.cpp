// Configuration of the concurrent requests layer for the size table. The chain is COBS over the stream link of SizeWire.h. The flags:
//   SIZE_ROLE   0: no layer; 1: responder (one command, Tickets(1), Refusals(2)); 2: requester (Open(2)).
//   SIZE_CONC   1: the layer is Pending(4) and Awaitable(), and the stack is Concurrent(); the stack's section is the port's section.
//   SIZE_AWAIT  1: the requester calls Await once (needs SIZE_CONC 1). The Await's wait is the difference of the two rows.
// The section and the tick are defined here as the STM32 port defines them (Periphery/Adapter/Port/STM32/System.cpp): the nesting helper of
// Periphery/System/NestedCriticalSection.h with the PRIMASK operations, and the tick and the RTOS delay handle.
#include "SizeWire.h"
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/Requests.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <System/NestedCriticalSection.h>
#include <functional>


#ifndef SIZE_ROLE
#define SIZE_ROLE 2
#endif

#ifndef SIZE_CONC
#define SIZE_CONC 0
#endif

#ifndef SIZE_AWAIT
#define SIZE_AWAIT 0
#endif


struct SizeInterrupts {
	static uint32 ReadMask() {
		uint32 mask = 0;
		asm volatile("mrs %0, PRIMASK" : "=r"(mask));
		return mask;
	}


	static void Disable() {
		asm volatile("cpsid i" ::: "memory");
	}


	static void Enable() {
		asm volatile("cpsie i" ::: "memory");
	}
};


void System::CriticalSection(bool isEnable) {
	if (isEnable) {
		NestedCriticalSection<SizeInterrupts>::Enter();
	} else {
		NestedCriticalSection<SizeInterrupts>::Leave();
	}
}


volatile uint32 System::tickLow = 0;
std::function<bool(uint32 delay)> System::rtosDelayMsHandle = nullptr;


enum class SizeStatus : uint8 {
	done = 0
};


struct SizeBody {
	uint16 value = 0;
};


enum class SizeCommand : uint8 {
	one = 0x10
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
			.TicketTimeoutMs(1000);
}>;


void OnServeOne(VUB::Ticket<SizeCommands, SizeCommand::one> ticket, const SizeBody& body) {
	(void)body;
	(void)ticket.Finish(SizeStatus::done);
}


void OnFinal(const VUB::Final<SizeStatus, VUB::NoBody>& reply) {
	sizeReceived = sizeReceived + static_cast<uint32>(reply.result);
}


#if SIZE_ROLE == 1
using SizeHandlers = VUB::Handlers<
	VUB::On<SizeCommand::one, &OnServeOne>>;
#else
using SizeHandlers = VUB::Handlers<>;
#endif


#if SIZE_CONC
#define SIZE_STEPS .Pending(4).Awaitable()
#define SIZE_STACK .Concurrent()
#else
#define SIZE_STEPS
#define SIZE_STACK
#endif


#if SIZE_ROLE == 1
#define SIZE_UPPER .Layer<VUB::Requests<SizeCommands, SizeHandlers>>().Tickets(1).Refusals(2) SIZE_STEPS
#elif SIZE_ROLE == 2
#define SIZE_UPPER .Layer<VUB::Requests<SizeCommands, SizeHandlers>>().Open(2) SIZE_STEPS
#else
#define SIZE_UPPER
#endif


using SizeStack = VUB::Stack<+[]() {
	return VUB::Builder()
		SIZE_UPPER
		.Framing<VUB::Cobs>()
		.Link<SizeStreamLink>()
		.MaxPayload(40)
		.OnReceive<&SizeReceive>()
		.Clock<&SizeClock>()
		SIZE_STACK;
}>;


SizeStreamLink sizeLink;
SizeStack sizeStack(sizeLink);


#if SIZE_ROLE == 2 && SIZE_AWAIT
template<typename HandleType>
void AwaitOnce(HandleType& handle) {
	SizeBody body{};
	VUB::Final<SizeStatus, VUB::NoBody> reply = handle.template Await<SizeCommand::one>(body);
	sizeReceived = sizeReceived + static_cast<uint32>(reply.result);
}
#endif


void SizeRun() {
#if SIZE_ROLE == 2
	auto handle = sizeStack.Requests();
	handle.SetSession(1);
	(void)handle.template Send<SizeCommand::one, &OnFinal>(SizeBody{});
#if SIZE_AWAIT
	AwaitOnce(handle);
#endif
#endif
	SizeRunStream(sizeStack);
}
