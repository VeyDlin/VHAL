#include "SizeSet.h"
#include <Utilities/AT/AtClient.h>
#include <optional>
#include <string_view>


// The client object of the size check: its reply handlers and URC handler, and the client that calls them. The entry point has external
// linkage, so the client's code stays in the object file.


void SizeOnTemp(Result<int16, AtStatus> result) {
	static_cast<void>(result);
}


void SizeOnMode(Result<int8, AtStatus> result) {
	static_cast<void>(result);
}


void SizeOnStatus(AtStatus status) {
	static_cast<void>(status);
}


void SizeOnIp() {
}


using SizeClientBinding = AtClientBinding<AtUrcOn<"WIFI GOT IP", &SizeOnIp>>;


void SizeClientSession(Print& out, uint8 byte) {
	AtClient<SizeSet, SizeClientBinding, AtQueue<2>> client(out);
	client.Feed(byte);
	client.Process(0);
	client.Read<"+TEMP", &SizeOnTemp>();
	client.Read<"+CWMODE", &SizeOnMode>();
	client.Write<"+LED", &SizeOnStatus>(true);
	client.Write<"+JOIN", &SizeOnStatus>(std::string_view("net"), std::optional<std::string_view>());
	client.Run<"+RUN", &SizeOnStatus>();
	client.Process(0);
}
