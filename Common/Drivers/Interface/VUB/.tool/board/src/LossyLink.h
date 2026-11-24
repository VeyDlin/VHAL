#pragma once
#include <Drivers/Interface/VUB/LinkState.h>
#include <Drivers/Interface/VUB/LinkTraits.h>
#include <Drivers/Interface/VUB/Receiver.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <cstddef>
#include <span>
#include <vector>


namespace VubBoard {
	// Frame ends of the stream framings the board chains use. Each returns the length of the first whole frame in the bytes, or 0 when the
	// frame is not complete yet. The bytes start at a frame boundary, because the stack writes whole frames in order.
	struct CobsFrames {
		// COBS ends a frame with its zero byte, which is the frame's last byte.
		static std::size_t FrameLength(std::span<const uint8> bytes) {
			for (std::size_t index = 0; index < bytes.size(); index++) {
				if (bytes[index] == 0) {
					return index + 1;
				}
			}

			return 0;
		}
	};


	struct LengthPrefixFrames {
		// VUB::LengthPrefix<2>: a sync byte, the payload length (two bytes, little-endian), a check byte, then the payload.
		static constexpr std::size_t HeaderBytes = 4;


		static std::size_t FrameLength(std::span<const uint8> bytes) {
			if (bytes.size() < HeaderBytes) {
				return 0;
			}

			std::size_t length = static_cast<std::size_t>(bytes[1]) | (static_cast<std::size_t>(bytes[2]) << 8);
			std::size_t total = HeaderBytes + length;
			return bytes.size() < total ? 0 : total;
		}
	};


	// A stream link that drops every k-th outgoing frame on the PC side, to exercise the resends of the delivery layer. The stack writes its
	// bytes into this link, which keeps them until they are whole frames. A frame that is not dropped goes to the inner link as the inner link
	// takes it, so the framing on the wire stays intact. Frames are counted from the first frame of the run, and dropped frames are counted.
	template<typename Inner, typename Frames>
	class LossyStream {
	public:
		static constexpr VUB::LinkTraits traits = Inner::traits;


	private:
		static constexpr std::size_t MaxPending = 4096;


		Inner* inner;
		std::vector<uint8> pending;
		std::size_t dropEvery = 0;
		std::size_t frames = 0;
		std::size_t dropped = 0;
		// Bytes of the frame at the front of pending that is being sent to the inner link. Zero when no frame is being sent.
		std::size_t sending = 0;


	public:
		explicit LossyStream(Inner& innerLink) : inner(&innerLink) {
			pending.reserve(MaxPending);
		}


		// Drops every k-th frame. Zero drops nothing.
		void SetDropEvery(std::size_t value) {
			dropEvery = value;
		}


		std::size_t Dropped() const {
			return dropped;
		}


		void SetStateSink(VUB::StateSink value) {
			inner->SetStateSink(value);
		}


		// Sends what is whole, then reads what the inner link has.
		void Poll(VUB::Receiver receiver) {
			Advance();
			inner->Poll(receiver);
		}


		// Takes the bytes into the pending buffer, up to its size, and sends the whole frames it can.
		Result<std::size_t> Write(std::span<const uint8> bytes) {
			std::size_t room = MaxPending - pending.size();
			std::size_t take = bytes.size() < room ? bytes.size() : room;
			pending.insert(pending.end(), bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(take));
			Advance();
			return take;
		}


	private:
		// Works through the pending frames. A frame whose number is a multiple of dropEvery is removed; the others go to the inner link.
		void Advance() {
			while (!pending.empty()) {
				if (sending == 0) {
					std::size_t length = Frames::FrameLength(std::span<const uint8>(pending.data(), pending.size()));
					if (length == 0) {
						return;
					}

					frames++;
					if (dropEvery > 0 && frames % dropEvery == 0) {
						pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(length));
						dropped++;
						continue;
					}

					sending = length;
				}

				Result<std::size_t> wrote = inner->Write(std::span<const uint8>(pending.data(), sending));
				if (wrote.IsErr() || wrote.Value() == 0) {
					return;
				}

				std::size_t count = wrote.Value();
				pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(count));
				sending -= count;
			}
		}
	};


	// A packet link that drops every k-th outgoing packet. A dropped packet is reported as sent, as a lost one is on a wire, so the delivery
	// layer sees no error and resends the packet when its acknowledgement does not come.
	template<typename Inner>
	class LossyPacket {
	public:
		static constexpr VUB::LinkTraits traits = Inner::traits;


	private:
		Inner* inner;
		std::size_t dropEvery = 0;
		std::size_t sends = 0;
		std::size_t dropped = 0;


	public:
		explicit LossyPacket(Inner& innerLink) : inner(&innerLink) {
		}


		// Drops every k-th send call. Zero drops nothing. A busy send is a call too, so a retry can be the one dropped.
		void SetDropEvery(std::size_t value) {
			dropEvery = value;
		}


		std::size_t Dropped() const {
			return dropped;
		}


		void SetStateSink(VUB::StateSink value) {
			inner->SetStateSink(value);
		}


		void Poll(VUB::Receiver receiver) {
			inner->Poll(receiver);
		}


		ResultStatus Send(std::span<const uint8> packet) {
			sends++;
			if (dropEvery > 0 && sends % dropEvery == 0) {
				dropped++;
				return ResultStatus::ok;
			}

			return inner->Send(packet);
		}
	};
}
