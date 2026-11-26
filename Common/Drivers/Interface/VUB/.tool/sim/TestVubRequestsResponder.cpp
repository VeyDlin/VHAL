#include "RequestsChains.h"
#include <Drivers/Interface/VUB/Outbox.h>
#include <Drivers/Interface/VUB/Requests.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <span>

// The responder side of the requests layer: the replies a handler gives, the tickets and their deadlines, the refusals a full table sends,
// and the fire and no-reply commands that take no ticket. The stacks and the commands are in RequestsChains.h.


// The responder alone, with one ticket: the stop takes it, so a second stop is refused busy.
using ServeLayer = VUB::RequestsOf<ServeCommands, ServeHandlers, VUB::RequestsOptions{0, 1, 4, false}>;


// A request answered at once: one packet goes back, and it carries the final, with the acknowledgement of the request.
void TestReplyAtOnce() {
	ResetRequestsState();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	Responder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	stopAtOnce = true;

	auto handle = near.Requests();
	auto sent = handle.Send<RequesterCommand::stop,&OnStopFinalR>(VUB::NoBody{});
	SIM_EXPECT(sent.IsErr() == false);
	SIM_EXPECT(sent.Value().number == 1);
	Run(line, near, far, log, 1000);
	SIM_EXPECT(stopsSeen == 1);
	SIM_EXPECT(stopFinals == 1);
	SIM_EXPECT(stopResult == ResultStatus::ok);
	// The request is a Reliable packet whose payload ends with motor.stop, number 1, tag 1; the reply is one packet ending with the final.
	std::array<uint8, 5> request = {0x01, 0x13, 0x01, 0x00, 0x01};
	std::array<uint8, 6> finalStarted = {0x03, 0x01, 0x00, 0x01, 0x00, 0x00};
	SIM_EXPECT(log.toFar.count == 1);
	SIM_EXPECT(EndsWith(log.toFar, 0, request));
	SIM_EXPECT(log.toNear.count == 1);
	SIM_EXPECT(EndsWith(log.toNear, 0, finalStarted));
	// The one packet of the reply carries the acknowledgement of the request: its flags have the acknowledgement bit, and it is not a bare ack.
	SIM_EXPECT((log.toNear.packets[0][0] & ReliableLayer::AcknowledgesFlag) != 0);
	SIM_EXPECT((log.toNear.packets[0][0] & ReliableLayer::ControlFlag) == 0);
}


// A request answered after its handler returns: nothing is final until the ticket finishes; then the final goes out in the next round.
void TestReplyLater() {
	ResetRequestsState();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	Responder far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	auto handle = near.Requests();
	SIM_EXPECT(SendStopR(handle));
	Run(line, near, far, log, 1000);
	Run(line, near, far, log, 2000);
	SIM_EXPECT(stopHeld);
	SIM_EXPECT(stopFinals == 0);
	SIM_EXPECT(heldStop.Open());
	SIM_EXPECT(heldStop.Finish(Motor::MotorStatus::started) == ResultStatus::ok);
	Run(line, near, far, log, 3000);
	SIM_EXPECT(stopFinals == 1);
	SIM_EXPECT(stopResult == ResultStatus::ok);
	SIM_EXPECT(heldStop.Open() == false);
}


// The device's ticket has its own deadline: when the request is never finished, the ticket is closed and its final is a timeout.
void TestTicketTimeout() {
	ResetRequestsState();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	Responder far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	auto handle = near.Requests();
	SIM_EXPECT(SendStopR(handle));
	Run(line, near, far, log, 1000);
	SIM_EXPECT(stopHeld);
	SIM_EXPECT(heldStop.Open());
	uint32 now = 1000;
	for (int step = 0; step < 25; step++) {
		now += 100000;
		Run(line, near, far, log, now);
	}

	SIM_EXPECT(heldStop.Open() == false);
	SIM_EXPECT(heldStop.Finish(Motor::MotorStatus::started) == ResultStatus::notAvailable);
}


// Refusals of the responder: the third stop finds both tickets taken (busy); an id the device does not serve is notSupported.
void TestRefusals() {
	ResetRequestsState();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	Responder far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	auto handle = near.Requests();
	SIM_EXPECT(SendStopR(handle));
	SIM_EXPECT(SendStopR(handle));
	Run(line, near, far, log, 1000);
	SIM_EXPECT(stopsSeen == 2);
	SIM_EXPECT(stopFinals == 0);
	SIM_EXPECT(SendStopR(handle));
	Run(line, near, far, log, 2000);
	SIM_EXPECT(stopFinals == 1);
	SIM_EXPECT(stopResult == ResultStatus::busy);

	auto unknown = handle.Send<RequesterCommand::unknown,&OnUnknownFinalR>(VUB::NoBody{});
	SIM_EXPECT(unknown.IsErr() == false);
	Run(line, near, far, log, 3000);
	SIM_EXPECT(unknownFinals == 1);
	SIM_EXPECT(unknownResult == ResultStatus::notSupported);
}


// A fire and a no-reply take no ticket. With the only ticket held by a stop, a no-reply runs at once, after the stop of the same burst; a fire
// runs at once with its body; a second stop finds no ticket and is refused busy, and nothing else is sent in that Process.
void TestFireAndNoReplyTakeNoTicket() {
	ResetServeState();
	ServeLayer layer;
	Probe probe;
	std::array<uint8, 5> stopOne = {0x01, 0x13, 0x01, 0x00, 0x01};
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(stopOne.data(), stopOne.size())).IsErr() == false);
	std::array<uint8, 5> resetTwo = {0x01, 0x14, 0x02, 0x00, 0x02};
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(resetTwo.data(), resetTwo.size())).IsErr() == false);
	SIM_EXPECT(serveStopHeld);
	SIM_EXPECT(serveResetCount == 1);
	SIM_EXPECT(serveOrderCount == 2);
	SIM_EXPECT(serveOrder[0] == 1);
	SIM_EXPECT(serveOrder[1] == 2);
	std::array<uint8, 5> stopThree = {0x01, 0x13, 0x03, 0x00, 0x03};
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(stopThree.data(), stopThree.size())).IsErr() == false);
	std::array<uint8, 6> kickFour = {0x01, 0x15, 0x04, 0x00, 0x04, 0x09};
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(kickFour.data(), kickFour.size())).IsErr() == false);
	SIM_EXPECT(serveKickCount == 1);
	SIM_EXPECT(serveKickCode == 9);
	layer.Process(1000, ProbeOutbox(probe));
	std::array<uint8, 5> busyThree = {0x03, 0x03, 0x00, 0x03, 0x04};
	SIM_EXPECT(probe.count == 1);
	SIM_EXPECT(ProbeLastIs(probe, busyThree));
	SIM_EXPECT(heldServeStop.Open());
	SIM_EXPECT(heldServeStop.Finish(Motor::MotorStatus::started) == ResultStatus::ok);
	layer.Process(2000, ProbeOutbox(probe));
	SIM_EXPECT(probe.count == 2);
}


// A bad body of a fire is returned as invalidData, with the handler not run; a good one runs the handler inside Take.
void TestFireBodyIsChecked() {
	ResetServeState();
	ServeLayer layer;
	Probe probe;
	std::array<uint8, 5> emptyKick = {0x01, 0x15, 0x05, 0x00, 0x05};
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(emptyKick.data(), emptyKick.size())).IsErr());
	SIM_EXPECT(serveKickCount == 0);
	layer.Process(1000, ProbeOutbox(probe));
	SIM_EXPECT(probe.count == 0);
}


// The slot of the one ticket takes 257 requests, each finished ahead of the next one. The generation is one byte, so the first request's
// generation and the generation of request 257 are the same, and the tags are the same too (both 1). Only the request number tells them
// apart: the first ticket is stale and answers notAvailable, and the request that holds the slot is untouched.
void TestStaleTicketAfterSlotReuse() {
	ResetServeState();
	ServeLayer layer;
	Probe probe;
	ServeStopTicket first{};
	for (uint16 number = 1; number <= 257; number++) {
		uint8 low = static_cast<uint8>(number & 0xFF);
		uint8 high = static_cast<uint8>(number >> 8);
		std::array<uint8, 5> stop = {0x01, 0x13, low, high, low};
		SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(stop.data(), stop.size())).IsErr() == false);
		layer.Process(1000 + number, ProbeOutbox(probe));
		SIM_EXPECT(serveStopHeld);
		if (number == 1) {
			first = heldServeStop;
		}

		if (number == 257) {
			break;
		}

		SIM_EXPECT(heldServeStop.Finish(Motor::MotorStatus::started) == ResultStatus::ok);
		layer.Process(1000 + number, ProbeOutbox(probe));
	}

	SIM_EXPECT(probe.count == 256);
	SIM_EXPECT(first.Open() == false);
	SIM_EXPECT(first.Finish(Motor::MotorStatus::started) == ResultStatus::notAvailable);
	SIM_EXPECT(first.Number().number == 1);
	SIM_EXPECT(heldServeStop.Open());
	SIM_EXPECT(heldServeStop.Number().number == 257);
	layer.Process(2000, ProbeOutbox(probe));
	SIM_EXPECT(probe.count == 256);
	SIM_EXPECT(heldServeStop.Finish(Motor::MotorStatus::started) == ResultStatus::ok);
	layer.Process(3000, ProbeOutbox(probe));
	std::array<uint8, 6> finalTwoFiftySeven = {0x03, 0x01, 0x01, 0x01, 0x00, 0x00};
	SIM_EXPECT(probe.count == 257);
	SIM_EXPECT(ProbeLastIs(probe, finalTwoFiftySeven));
}


int main() {
	TestReplyAtOnce();
	TestReplyLater();
	TestTicketTimeout();
	TestRefusals();
	TestFireAndNoReplyTakeNoTicket();
	TestFireBodyIsChecked();
	TestStaleTicketAfterSlotReuse();
	return SimFinish("TestVubRequestsResponder");
}
