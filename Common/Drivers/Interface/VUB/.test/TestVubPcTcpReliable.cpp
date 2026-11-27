// libs-windows: ws2_32
#include "SimCheck.h"
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/Layer/Reliable.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <Drivers/Interface/VUB/PC/TcpLink.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <thread>


// The delivery layer (VUB::Reliable) over the TCP link, on 127.0.0.1. The listener and the client each carry three packets, and each packet
// is delivered once and acknowledged. The layer's handlers are plain functions (not inline): some compilers need that for the core's
// Reliable chain, and the Reliable chain is otherwise the same as the COBS chain of TestVubPcTcp.cpp.


// The packets delivered to the receive handler, and the outcomes of the sent ones.
inline std::size_t reliableDelivered = 0;
inline std::size_t reliableOutcomesOk = 0;
inline std::size_t reliableOutcomesFailed = 0;


inline uint32 ReliableTcpClockUs() {
	std::chrono::microseconds now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch());
	return static_cast<uint32>(now.count());
}


void ReliableTcpReceive(const VUB::Meta&, std::span<const uint8> payload) {
	(void)payload;
	reliableDelivered++;
}


void ReliableTcpOutcome(const VUB::Outcome& outcome) {
	if (outcome.status == ResultStatus::ok) {
		reliableOutcomesOk++;
	} else {
		reliableOutcomesFailed++;
	}
}


void ReliableTcpRestart() {
}


using TcpReliableStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<VUB::Reliable>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(32).OnOutcome<&ReliableTcpOutcome>().OnRestart<&ReliableTcpRestart>()
		.Framing<VUB::Cobs>()
		.Link<VUB::PC::TcpLink>()
		.MaxPayload(40)
		.ReceiveRing(256)
		.OnReceive<&ReliableTcpReceive>()
		.Clock<&ReliableTcpClockUs>();
}>;


// Processes both stacks until both links are up. Returns false when they are not up after a while.
template<typename StackType>
bool WaitUp(StackType& first, StackType& second) {
	for (int step = 0; step < 1000; step++) {
		first.Process();
		second.Process();
		if (first.LinkUp() && second.LinkUp()) {
			return true;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	return false;
}


// Sends a packet. The delivery layer answers busy while its session starts, so the stacks are processed and the send is tried again.
template<typename StackType>
bool SendWhenReady(StackType& sender, StackType& other, std::span<const uint8> packet) {
	for (int step = 0; step < 1000; step++) {
		if (sender.Send(packet) == ResultStatus::ok) {
			return true;
		}

		sender.Process();
		other.Process();
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	return false;
}


inline void ReliableOverTcp() {
	VUB::PC::TcpLink listenerLink;
	VUB::PC::TcpLink clientLink;
	TcpReliableStack serverStack(listenerLink);
	TcpReliableStack clientStack(clientLink);
	serverStack.Find<VUB::Reliable>().SetSession(0x1111);
	clientStack.Find<VUB::Reliable>().SetSession(0x2222);
	SIM_EXPECT(listenerLink.Listen("127.0.0.1", 0) == ResultStatus::ok);
	SIM_EXPECT(clientLink.Connect("127.0.0.1", listenerLink.BoundPort()) == ResultStatus::ok);
	SIM_EXPECT(WaitUp(serverStack, clientStack));

	for (uint8 index = 0; index < 3; index++) {
		const std::array<uint8, 2> packet = {index, 0x55};
		SIM_EXPECT(SendWhenReady(clientStack, serverStack, packet));
	}

	for (int step = 0; step < 1000 && (reliableDelivered < 3 || reliableOutcomesOk < 3); step++) {
		serverStack.Process();
		clientStack.Process();
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	SIM_EXPECT(reliableDelivered == 3);
	SIM_EXPECT(reliableOutcomesOk == 3);
	SIM_EXPECT(reliableOutcomesFailed == 0);
}


int main() {
	ReliableOverTcp();
	return SimFinish("TestVubPcTcpReliable");
}
