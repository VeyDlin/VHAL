#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "UsbBackend.h"

enum class Command : std::uint8_t {
	None,
	Info,
	Counters,
	In,
	Out,
	Loopback,
	SourceSink,
	Reconnect
};

inline constexpr std::uint32_t DefaultInflight = 8;
inline constexpr std::uint32_t MaxInflight = 32;
inline constexpr std::size_t DefaultTransferBytes = 65536;
inline constexpr std::size_t MaxTransferBytes = 1024 * 1024;
inline constexpr std::uint64_t MaxReconnectMs = 60000;

struct Options {
	Command command = Command::None;
	BackendKind backend = BackendKind::LibUsb;
	std::uint32_t inflight = DefaultInflight;
	std::size_t transferSize = DefaultTransferBytes;
	std::uint64_t bytes = 0;
	std::uint64_t reconnectMs = 0;
};

enum class ParseResult : std::uint8_t {
	Ok,
	Help,
	Error
};

ParseResult ParseOptions(int argc, char** argv, Options& options, std::string& error);

std::string UsageText();
