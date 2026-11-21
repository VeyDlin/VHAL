#pragma once

#include <cstdint>
#include <vector>

// winsock2.h must come before windows.h, which every file that includes this header needs.
#include <winsock2.h>
#include <windows.h>

#include "NetworkLogic.h"

// Sends ICMP echo requests through IcmpSendEcho2Ex, so each request leaves with the source address the caller names. Owns the
// IcmpCreateFile handle.
class EchoSender {
private:
	HANDLE handle = INVALID_HANDLE_VALUE;


public:
	EchoSender() = default;

	EchoSender(const EchoSender&) = delete;
	EchoSender& operator=(const EchoSender&) = delete;

	~EchoSender();

	// False with an error when the ICMP handle cannot be opened.
	bool Open(std::string& error);

	// Sends one echo with the payload from sourceAddress to destinationAddress (both in host order) and waits up to timeoutMs. The
	// result says whether a reply came, or why not (EchoResult::problem).
	EchoResult Send(
		std::uint32_t sourceAddress,
		std::uint32_t destinationAddress,
		const std::vector<std::uint8_t>& payload,
		std::uint32_t timeoutMs) const;
};
