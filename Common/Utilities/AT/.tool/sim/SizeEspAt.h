#pragma once
#include <Utilities/AT/AtClient.h>
#include <Utilities/AT/Descriptions/EspAt.h>
#include <optional>
#include <span>
#include <string_view>


// The shared parts of the ESP-AT client size check: the URC handlers, the reply handlers and the data of the SendData call. Each size
// object is one client with a set of modules, built from the same handlers, so the object's size is the client's code for those calls.


inline void SizeEspAtReady() {
}


inline void SizeEspAtGotIp() {
}


inline void SizeEspAtIpd(uint16 length, uint16 offset, std::span<const uint8> chunk, bool last) {
	static_cast<void>(length);
	static_cast<void>(offset);
	static_cast<void>(chunk);
	static_cast<void>(last);
}


inline void SizeEspAtMode(Result<uint8, AtStatus> result) {
	static_cast<void>(result);
}


inline void SizeEspAtStatus(AtStatus status) {
	static_cast<void>(status);
}


inline void SizeEspAtAddress(Result<String<15>, AtStatus> result) {
	static_cast<void>(result);
}


inline void SizeEspAtJoined(Result<EspAtJoinedNetwork, AtStatus> result) {
	static_cast<void>(result);
}


inline void SizeEspAtScan(const Result<AtRowList<EspAtAccessPoint, 8>, AtStatus>& result) {
	static_cast<void>(result);
}


using SizeEspAtBinding = AtClientBinding<
	AtUrcOn<"WIFI GOT IP", &SizeEspAtGotIp>,
	AtUrcOn<"+IPD", &SizeEspAtIpd>
>;


inline constexpr uint8 SizeEspAtData[] = {'h', 'e', 'l', 'l', 'o'};
