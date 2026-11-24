#pragma once
#include <Drivers/Interface/VUB/LinkState.h>
#include <Drivers/Interface/VUB/LinkTraits.h>
#include <Drivers/Interface/VUB/Receiver.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <array>
#include <cstddef>
#include <span>

namespace VUB {


// A byte-stream link over a CDC-ACM function (Common/Drivers/Interface/USB/Class/CDC/CdcAcmFunction.h). The template takes the
// function type, so any function with the same members works: Write, Read, IsConfigured and IsTerminalOpen.
//
// The link is a stream, reliable and full duplex, and it reports its state. The link is up while the function is configured and the
// host has DTR set, the signal of an open terminal. The state is sampled in Poll, which runs in Process, and the sink is told of a change
// only. A session that ends and starts again between two samples, by a bus reset or by a deconfigure and a configure, is a fall and then
// a rise when the link was up, because the function's session generation changed. Poll reads from the function up to the receiver's room,
// in pieces of readChunkBytes, so the bytes stay in the function's ring when the stack has no room. Write is the function's Write: it
// queues what the ring takes and returns 0 when the ring is full or the function is not configured.
//
// MaxPacketBytes is the largest packet of the link, which is the stack's buffer for a packet of the framing above it.
template<typename Cdc, std::size_t MaxPacketBytes = 64>
class CdcLink {
	static_assert(MaxPacketBytes > 0, "the largest packet of a CDC link is at least one byte");

public:
	static constexpr LinkTraits traits = LinkTraits().MaxPacket(MaxPacketBytes).Reliable().FullDuplex().ReportsState().Starts(Initiator::Either);


private:
	static constexpr std::size_t readChunkBytes = 64;

	Cdc* function;
	StateSink sink;
	bool reportedUp = false;
	uint32 sessionGeneration = 0;
	std::array<uint8, readChunkBytes> readBuffer{};


public:
	explicit CdcLink(Cdc& usbFunction) : function(&usbFunction) {
	}


	// The stack gives its state sink here. The link reports its state at once, so the stack knows it from the first Process.
	void SetStateSink(StateSink value) {
		sink = value;
		sessionGeneration = function->GetSessionGeneration();
		reportedUp = IsUp();
		sink.Report(reportedUp);
	}


	// Samples the state, then reads what the host sent, up to the receiver's room, and feeds it to the receiver.
	void Poll(Receiver receiver) {
		SampleState();
		std::size_t room = receiver.Room();
		while (room > 0) {
			std::size_t take = room < readChunkBytes ? room : readChunkBytes;
			std::size_t got = function->Read(std::span<uint8>(readBuffer.data(), take));
			if (got == 0) {
				return;
			}

			receiver.Feed(std::span<const uint8>(readBuffer.data(), got));
			room = room - got;
		}
	}


	// Queues the bytes the function takes. Returns 0 while the function does not take any.
	Result<std::size_t> Write(std::span<const uint8> bytes) {
		return static_cast<std::size_t>(function->Write(bytes));
	}


private:
	bool IsUp() const {
		return function->IsConfigured() && function->IsTerminalOpen();
	}


	// A changed session generation ends the session the link was up in: a fall is reported before the state is compared, so a
	// session that restarted between two samples is a fall and then a rise.
	void SampleState() {
		uint32 generation = function->GetSessionGeneration();
		bool up = IsUp();
		if (generation != sessionGeneration) {
			sessionGeneration = generation;
			if (reportedUp) {
				reportedUp = false;
				sink.Report(false);
			}
		}

		if (up != reportedUp) {
			reportedUp = up;
			sink.Report(up);
		}
	}
};

}
