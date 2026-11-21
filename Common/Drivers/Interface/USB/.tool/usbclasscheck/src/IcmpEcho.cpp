#include "IcmpEcho.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// iphlpapi.h defines the IP address types that icmpapi.h uses, so it comes first.
#include <iphlpapi.h>
#include <icmpapi.h>
#include <winternl.h>

#include "NetworkLogic.h"
#include "WinText.h"

namespace {

// Windows may add the first 8 bytes of the packet that caused an error to the reply, so the reply buffer keeps room for them.
constexpr std::size_t IcmpErrorDataBytes = 8;
// IcmpSendEcho2Ex also writes an IO_STATUS_BLOCK after the replies, and the documentation asks for room for it in the reply buffer.
// Without it the write can run past the end of the buffer.
constexpr std::size_t IcmpStatusBlockBytes = sizeof(IO_STATUS_BLOCK);

}


EchoSender::~EchoSender() {
	if (handle != INVALID_HANDLE_VALUE) {
		IcmpCloseHandle(handle);
	}
}


bool EchoSender::Open(std::string& error) {
	handle = IcmpCreateFile();
	if (handle == INVALID_HANDLE_VALUE) {
		error = "IcmpCreateFile failed: " + FormatWindowsError(GetLastError());
		return false;
	}

	return true;
}


EchoResult EchoSender::Send(
	std::uint32_t sourceAddress,
	std::uint32_t destinationAddress,
	const std::vector<std::uint8_t>& payload,
	std::uint32_t timeoutMs) const
{
	EchoResult result;

	// An empty payload still gets a valid pointer; the size passed is zero, so Windows sends no data. Windows only reads the request
	// data, but the API declares it as LPVOID, so the const is cast away for the call.
	const std::uint8_t NoPayload[1] = {0};
	const std::uint8_t* requestData = payload.empty() ? NoPayload : payload.data();
	std::vector<std::uint8_t> reply(sizeof(ICMP_ECHO_REPLY) + payload.size() + IcmpErrorDataBytes + IcmpStatusBlockBytes);
	DWORD replies = IcmpSendEcho2Ex(
		handle,
		nullptr,
		nullptr,
		nullptr,
		htonl(sourceAddress),
		htonl(destinationAddress),
		const_cast<std::uint8_t*>(requestData),
		static_cast<WORD>(payload.size()),
		nullptr,
		reply.data(),
		static_cast<DWORD>(reply.size()),
		timeoutMs);

	if (replies == 0) {
		DWORD code = GetLastError();
		if (code == IP_REQ_TIMED_OUT) {
			result.problem = "no reply";
		} else {
			result.problem = "the request was not sent: " + FormatWindowsError(code);
		}

		return result;
	}

	const ICMP_ECHO_REPLY* echo = reinterpret_cast<const ICMP_ECHO_REPLY*>(reply.data());
	result.replied = true;
	result.source = ntohl(echo->Address);
	result.status = echo->Status;
	result.roundTripMs = echo->RoundTripTime;
	const std::uint8_t* data = reinterpret_cast<const std::uint8_t*>(echo->Data);
	result.data.assign(data, data + echo->DataSize);
	return result;
}
