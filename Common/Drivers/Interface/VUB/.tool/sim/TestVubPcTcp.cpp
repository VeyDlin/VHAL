// libs-windows: ws2_32
#include "SimCheck.h"
#include <Drivers/Interface/VUB/Framing/Cobs.h>
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


// Two stacks over two TcpLinks on 127.0.0.1: a listener and a client. Every test binds the port the system picks, so no test depends on a
// port number. Both stacks are polled in one thread: Process reads what has arrived, so the tests step both until the state they wait for
// holds. The delivery layer over TCP is in TestVubPcTcpReliable.cpp.


// The payload of the last received packet, and how many were received.
inline std::array<uint8, 64> tcpLast{};
inline std::size_t tcpLastLength = 0;
inline std::size_t tcpReceivedCount = 0;


inline uint32 TcpClockUs() {
	std::chrono::microseconds now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch());
	return static_cast<uint32>(now.count());
}


inline void TcpReceive(const VUB::Meta&, std::span<const uint8> payload) {
	tcpReceivedCount++;
	tcpLastLength = payload.size() < tcpLast.size() ? payload.size() : tcpLast.size();
	for (std::size_t index = 0; index < tcpLastLength; index++) {
		tcpLast[index] = payload[index];
	}
}


// COBS framing over the TCP link.
using TcpStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Cobs>()
		.Link<VUB::PC::TcpLink>()
		.MaxPayload(40)
		.ReceiveRing(256)
		.OnReceive<&TcpReceive>()
		.Clock<&TcpClockUs>();
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


// Processes both stacks until the handler has counted the given number of packets.
template<typename StackType>
bool RunUntilReceived(StackType& first, StackType& second, std::size_t count) {
	for (int step = 0; step < 1000; step++) {
		first.Process();
		second.Process();
		if (tcpReceivedCount >= count) {
			return true;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	return false;
}


// Processes both stacks until the link of the first is down, after the peer closed it.
template<typename StackType>
bool RunUntilDown(StackType& first, StackType& second) {
	for (int step = 0; step < 1000; step++) {
		first.Process();
		second.Process();
		if (!first.LinkUp()) {
			return true;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	return false;
}


// The listener accepts the client, the stacks carry a packet each way, and the bytes are the same.
inline void EndToEnd() {
	VUB::PC::TcpLink listenerLink;
	VUB::PC::TcpLink clientLink;
	TcpStack serverStack(listenerLink);
	TcpStack clientStack(clientLink);
	SIM_EXPECT(listenerLink.Listen("127.0.0.1", 0) == ResultStatus::ok);
	std::uint16_t port = listenerLink.BoundPort();
	SIM_EXPECT(port != 0);
	SIM_EXPECT(clientLink.Connect("127.0.0.1", port) == ResultStatus::ok);
	SIM_EXPECT(WaitUp(serverStack, clientStack));
	SIM_EXPECT(listenerLink.IsConnected());
	SIM_EXPECT(clientLink.IsConnected());

	const std::array<uint8, 5> first = {1, 2, 3, 4, 5};
	SIM_EXPECT(clientStack.Send(first) == ResultStatus::ok);
	SIM_EXPECT(RunUntilReceived(serverStack, clientStack, 1));
	SIM_EXPECT(tcpLastLength == 5);
	SIM_EXPECT(tcpLast[0] == 1 && tcpLast[4] == 5);

	const std::array<uint8, 3> second = {9, 8, 7};
	SIM_EXPECT(serverStack.Send(second) == ResultStatus::ok);
	SIM_EXPECT(RunUntilReceived(clientStack, serverStack, 2));
	SIM_EXPECT(tcpLastLength == 3);
	SIM_EXPECT(tcpLast[0] == 9 && tcpLast[2] == 7);
}


// The client closes: the listener sees the link go down. The client connects again, the listener accepts it without a new Listen, and a
// packet crosses again.
inline void DisconnectAndReconnect() {
	VUB::PC::TcpLink listenerLink;
	VUB::PC::TcpLink clientLink;
	TcpStack serverStack(listenerLink);
	TcpStack clientStack(clientLink);
	SIM_EXPECT(listenerLink.Listen("127.0.0.1", 0) == ResultStatus::ok);
	std::uint16_t port = listenerLink.BoundPort();
	SIM_EXPECT(clientLink.Connect("127.0.0.1", port) == ResultStatus::ok);
	SIM_EXPECT(WaitUp(serverStack, clientStack));

	clientLink.Close();
	SIM_EXPECT(RunUntilDown(serverStack, clientStack));
	SIM_EXPECT(!serverStack.LinkUp());
	SIM_EXPECT(!listenerLink.IsConnected());

	SIM_EXPECT(clientLink.Connect("127.0.0.1", port) == ResultStatus::ok);
	SIM_EXPECT(WaitUp(serverStack, clientStack));

	const std::size_t before = tcpReceivedCount;
	const std::array<uint8, 1> again = {42};
	SIM_EXPECT(clientStack.Send(again) == ResultStatus::ok);
	SIM_EXPECT(RunUntilReceived(serverStack, clientStack, before + 1));
	SIM_EXPECT(tcpLast[0] == 42);
}


// A connection to a port with nothing listening is refused: within a bounded number of steps the link is no longer connecting, and it is
// not up. Connect then works again on the same link, and a connection to a listener comes up.
inline void RefusedConnect() {
	VUB::PC::TcpLink probe;
	SIM_EXPECT(probe.Listen("127.0.0.1", 0) == ResultStatus::ok);
	std::uint16_t port = probe.BoundPort();
	probe.Close();

	VUB::PC::TcpLink lonelyLink;
	TcpStack lonelyStack(lonelyLink);
	SIM_EXPECT(lonelyLink.Connect("127.0.0.1", port) == ResultStatus::ok);
	bool settled = false;
	for (int step = 0; step < 200 && !settled; step++) {
		lonelyStack.Process();
		settled = !lonelyLink.IsConnecting();
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	SIM_EXPECT(settled);
	SIM_EXPECT(!lonelyLink.IsConnected());
	SIM_EXPECT(!lonelyStack.LinkUp());

	VUB::PC::TcpLink listenerLink;
	TcpStack serverStack(listenerLink);
	SIM_EXPECT(listenerLink.Listen("127.0.0.1", 0) == ResultStatus::ok);
	SIM_EXPECT(lonelyLink.Connect("127.0.0.1", listenerLink.BoundPort()) == ResultStatus::ok);
	SIM_EXPECT(WaitUp(serverStack, lonelyStack));
}


// A name that resolves to an IPv6 address and an IPv4 address connects to the IPv4 listener: Windows resolves "localhost" to ::1 first, and
// the link tries the next address when the first is refused. A name whose addresses all refuse the connection is reported as not up.
inline void ConnectsByName() {
	VUB::PC::TcpLink listenerLink;
	VUB::PC::TcpLink clientLink;
	TcpStack serverStack(listenerLink);
	TcpStack clientStack(clientLink);
	SIM_EXPECT(listenerLink.Listen("127.0.0.1", 0) == ResultStatus::ok);
	std::uint16_t port = listenerLink.BoundPort();
	SIM_EXPECT(clientLink.Connect("localhost", port) == ResultStatus::ok);
	SIM_EXPECT(WaitUp(serverStack, clientStack));
	SIM_EXPECT(clientLink.IsConnected());

	VUB::PC::TcpLink probe;
	SIM_EXPECT(probe.Listen("127.0.0.1", 0) == ResultStatus::ok);
	std::uint16_t closedPort = probe.BoundPort();
	probe.Close();

	VUB::PC::TcpLink lonelyLink;
	TcpStack lonelyStack(lonelyLink);
	SIM_EXPECT(lonelyLink.Connect("localhost", closedPort) == ResultStatus::ok);
	bool settled = false;
	for (int step = 0; step < 400 && !settled; step++) {
		lonelyStack.Process();
		settled = !lonelyLink.IsConnecting();
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	SIM_EXPECT(settled);
	SIM_EXPECT(!lonelyLink.IsConnected());
}


// The status of a connect that failed for every address is noConnection when at least one attempt got a socket and the connection was
// refused, and ioError only when no attempt got a socket. The order of the attempts does not change it.
inline void FailureStatusFollowsTheAttempts() {
	using Attempt = VUB::PC::TcpLink::Attempt;
	const std::array<Attempt, 2> noSocketThenRefused = { Attempt::noSocket, Attempt::refused };
	const std::array<Attempt, 2> refusedThenNoSocket = { Attempt::refused, Attempt::noSocket };
	const std::array<Attempt, 2> twoNoSockets = { Attempt::noSocket, Attempt::noSocket };
	const std::array<Attempt, 2> twoRefused = { Attempt::refused, Attempt::refused };
	SIM_EXPECT(VUB::PC::TcpLink::FailureStatus(noSocketThenRefused) == ResultStatus::noConnection);
	SIM_EXPECT(VUB::PC::TcpLink::FailureStatus(refusedThenNoSocket) == ResultStatus::noConnection);
	SIM_EXPECT(VUB::PC::TcpLink::FailureStatus(twoNoSockets) == ResultStatus::ioError);
	SIM_EXPECT(VUB::PC::TcpLink::FailureStatus(twoRefused) == ResultStatus::noConnection);
	SIM_EXPECT(VUB::PC::TcpLink::FailureStatus(std::span<const Attempt>()) == ResultStatus::ioError);
}


int main() {
	EndToEnd();
	DisconnectAndReconnect();
	RefusedConnect();
	ConnectsByName();
	FailureStatusFollowsTheAttempts();
	return SimFinish("TestVubPcTcp");
}
