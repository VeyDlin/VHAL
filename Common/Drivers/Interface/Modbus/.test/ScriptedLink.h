#pragma once
#include <Drivers/Interface/Modbus/Transport.h>
#include <Utilities/Checksum/CRC/Preset/Crc16Modbus.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>


// A transport with no timing. Each Receive gives the next scripted reply: a whole frame, or a status such as timeout or readError.
// Each Send is recorded as the bytes the client put on the line. Used to test the client and the server function by function.
class ScriptedLink {
public:
	struct Reply {
		bool isFrame;
		ResultStatus status;
		std::vector<uint8> frame;
	};


private:
	std::vector<Reply> replies;
	std::vector<std::vector<uint8>> sent;
	std::vector<uint32> timeouts;
	std::size_t nextReply = 0;
	ResultStatus sendStatus = ResultStatus::ok;


public:
	ResultStatus Begin() {
		return ResultStatus::ok;
	}


	// The wait for a quiet line is not modelled: a scripted line is always quiet, so the timeout is not used.
	ResultStatus Send(std::span<const uint8> frame, uint32) {
		sent.emplace_back(frame.begin(), frame.end());
		return sendStatus;
	}


	Result<uint16> Receive(std::span<uint8> frame, uint32 timeoutUs) {
		timeouts.push_back(timeoutUs);
		if (nextReply >= replies.size()) {
			return ResultStatus::timeout;
		}

		Reply reply = replies[nextReply];
		nextReply++;

		if (!reply.isFrame) {
			return reply.status;
		}

		std::copy(reply.frame.begin(), reply.frame.end(), frame.begin());
		return static_cast<uint16>(reply.frame.size());
	}


	// Queues a whole reply frame.
	void PushFrame(std::vector<uint8> frame) {
		replies.push_back(Reply{true, ResultStatus::ok, std::move(frame)});
	}


	// Queues a receive failure.
	void PushStatus(ResultStatus status) {
		replies.push_back(Reply{false, status, {}});
	}


	void SetSendStatus(ResultStatus status) {
		sendStatus = status;
	}


	const std::vector<std::vector<uint8>>& Sent() const {
		return sent;
	}


	const std::vector<uint32>& Timeouts() const {
		return timeouts;
	}
};


// Appends the Modbus CRC-16 (low byte first) to a frame body, using the library's own CRC. The framer is checked against this CRC in
// the RTU tests, so the frames built here are independent of the framer under test.
inline std::vector<uint8> ScriptedSeal(std::vector<uint8> body) {
	uint16 crc = Checksum::Crc16Modbus::Calculate(body);
	body.push_back(static_cast<uint8>(crc & 0xFF));
	body.push_back(static_cast<uint8>(crc >> 8));
	return body;
}
