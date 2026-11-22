#pragma once
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <concepts>
#include <cstddef>
#include <span>


// The longest the server waits for the line to go quiet before it sends a reply, in microseconds.
constexpr uint32 ModbusReplyWaitUs = 1000000;


// What the Modbus client and server need from a transport. A transport moves whole frames: Send puts one frame on the line, and
// Receive returns one complete frame. Begin is called once, before the first Send.
//
// Send waits at most timeoutUs microseconds for the line to go quiet, then returns once the last byte has left the line. It returns
// ok, writeError, or timeout when the line stayed busy for the whole wait; in that case it sends nothing.
// Receive waits at most timeoutUs microseconds (0 returns at once) and returns the frame length, timeout when no complete frame is
// ready, or invalidData when the frame broke on the line.
template<typename Transport>
concept ModbusTransport = requires(Transport& transport, std::span<const uint8> out, std::span<uint8> in, uint32 timeoutUs) {
	{ transport.Begin() } -> std::same_as<ResultStatus>;
	{ transport.Send(out, timeoutUs) } -> std::same_as<ResultStatus>;
	{ transport.Receive(in, timeoutUs) } -> std::same_as<Result<uint16>>;
};
