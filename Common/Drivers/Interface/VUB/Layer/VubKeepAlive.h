#pragma once
#include <Drivers/Interface/VUB/VubBlocks.h>
#include <Drivers/Interface/VUB/VubMeta.h>
#include <Drivers/Interface/VUB/VubOutbox.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>


// The keep-alive of a link that cannot report its state, such as a UART or an RS-485 line. It is a layer of one hop: it sits below the
// address and never forwards a packet. When nothing has been received from the peer for the quiet time, it sends a ping. When nothing has
// been received for the down time, it declares the peer down through the outbox, and the next packet received declares it up again. The
// peer's keep-alive answers each ping with a pong, so a quiet peer that is alive stays up. Any packet that reaches the layer counts as
// life, including one the layer refuses.
//
// The packets. The layer's header is one byte. A data packet starts with 0x00 and carries the payload after it. A ping is the single byte
// 0x01, and a pong is the single byte 0x02. A packet with any other first byte, or a ping or pong with more bytes, is refused and counted
// under layerError.
//
// The layer's own packets go while only the peer is down, so a ping reaches the peer while both ends have declared the other down. Put the
// layer above the link's CRC, so that a corrupted packet is dropped before it counts as life.
//
// The times are options, given after the layer in the chain with QuietTime(us) and DownTime(us), in microseconds. The down time should be
// longer than the quiet time, so that a ping and its pong fit before the peer is declared down.
//
// On a link that reports its state, the layer asks the link at each Process (outbox.LinkUp). While the link is down it sends nothing,
// declares nothing and forgets what it heard; when the link comes up its timers start again from that Process, so the first ping goes
// one quiet time later. A link that does not report its state is always up, and the query answers true.
struct VubKeepAlive {
	struct Options {
		uint32 quietUs = 1000000;
		uint32 downUs = 3000000;
	};


	// The options of the layer, after the layer in the chain: each method reads them with LastOptions, changes one field, and returns the
	// Step over WithLastOptions.
	template<typename Builder>
	struct Step : Builder {
		explicit constexpr Step(const Builder& base) : Builder(base) {
		}


		constexpr Step QuietTime(uint32 value) const {
			Options options = this->LastOptions();
			options.quietUs = value;
			return Step(this->WithLastOptions(options));
		}


		constexpr Step DownTime(uint32 value) const {
			Options options = this->LastOptions();
			options.downUs = value;
			return Step(this->WithLastOptions(options));
		}
	};


	static constexpr std::size_t header = 1;
	static constexpr uint32 fingerprint = 0x4B414C31;
	static constexpr bool usesTime = true;
	static constexpr bool makesBulk = false;
	static constexpr bool reportsPeer = true;
	static constexpr bool usesLinkState = true;
	static constexpr uint8 KindData = 0x00;
	static constexpr uint8 KindPing = 0x01;
	static constexpr uint8 KindPong = 0x02;


private:
	uint32 quietUs = 0;
	uint32 downUs = 0;
	uint32 heardUs = 0;
	uint32 pingUs = 0;
	bool restart = true;
	bool heard = false;
	bool pongDue = false;
	bool peerUp = true;


public:
	explicit VubKeepAlive(const Options& value) : quietUs(value.quietUs), downUs(value.downUs) {
	}


	Result<std::size_t> Add(const VubMeta&, std::span<const uint8> in, std::span<uint8> out) {
		if (out.size() < in.size() + header) {
			return ResultStatus::invalidParameter;
		}

		out[0] = KindData;
		for (std::size_t index = 0; index < in.size(); index++) {
			out[header + index] = in[index];
		}

		return in.size() + header;
	}


	// Every packet that reaches the layer is life. A ping sets a pong to send from Process; the packet is the layer's own and is not passed
	// up.
	Result<VubTake> Take(const VubMeta&, std::span<const uint8> packet) {
		heard = true;
		if (packet.empty()) {
			return ResultStatus::invalidData;
		}

		if (packet[0] == KindData) {
			return VubTake{header, true};
		}

		if (packet.size() == 1 && packet[0] == KindPing) {
			pongDue = true;
			return VubTake{header, false};
		}

		if (packet.size() == 1 && packet[0] == KindPong) {
			return VubTake{header, false};
		}

		return ResultStatus::invalidData;
	}


	// Runs once per Process, after the packets were taken. While the physical link is down, nothing is sent, nothing is declared, and what
	// was heard and any pong due are dropped. The first Process that finds the link up starts both timers from its time. A pong waits
	// until it is sent. A ping goes at each quiet time while nothing is heard, and not sooner than a quiet time after the last one, so a
	// peer that is down gets a ping at each quiet time. The ping is kept for the next Process when the outbox refuses it.
	void Process(uint32 nowUs, const VubOutbox& outbox) {
		if (!outbox.LinkUp()) {
			restart = true;
			heard = false;
			pongDue = false;
			return;
		}

		if (restart) {
			restart = false;
			heardUs = nowUs;
			pingUs = nowUs;
		}

		if (heard) {
			heard = false;
			heardUs = nowUs;
			if (!peerUp) {
				peerUp = true;
				outbox.SetPeer(true);
			}
		}

		if (pongDue) {
			std::array<uint8, 1> pong = {KindPong};
			if (outbox.Send(std::span<const uint8>(pong.data(), pong.size())) == ResultStatus::ok) {
				pongDue = false;
			}
		}

		if (peerUp && nowUs - heardUs >= downUs) {
			peerUp = false;
			outbox.SetPeer(false);
		}

		if (nowUs - heardUs >= quietUs && nowUs - pingUs >= quietUs) {
			std::array<uint8, 1> ping = {KindPing};
			if (outbox.Send(std::span<const uint8>(ping.data(), ping.size())) == ResultStatus::ok) {
				pingUs = nowUs;
			}
		}
	}
};
