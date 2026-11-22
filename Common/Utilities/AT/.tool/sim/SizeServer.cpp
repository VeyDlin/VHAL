#include "SizeSet.h"
#include <Utilities/AT/AtServer.h>
#include <optional>
#include <string_view>


// The server object of the size check: its handlers, and the server that runs them. The entry point has external linkage, so the
// server's code stays in the object file.


struct SizeApp {
	int16 temperature = 0;
	int8 mode = 0;
};


Result<int16, AtStatus> SizeTemp(AtCall<SizeApp> call) {
	return call.app.temperature;
}


AtStatus SizeLed(AtCall<SizeApp> call, bool on) {
	static_cast<void>(call);
	static_cast<void>(on);
	return AtStatus{};
}


Result<int8, AtStatus> SizeModeRead(AtCall<SizeApp> call) {
	return call.app.mode;
}


AtStatus SizeModeWrite(AtCall<SizeApp> call, int8 mode) {
	call.app.mode = mode;
	return AtStatus{};
}


AtStatus SizeJoin(AtCall<SizeApp> call, std::string_view ssid, std::optional<std::string_view> password) {
	static_cast<void>(call);
	static_cast<void>(ssid);
	static_cast<void>(password);
	return AtStatus{};
}


AtStatus SizeRun(AtCall<SizeApp> call) {
	static_cast<void>(call);
	return AtStatus{};
}


using SizeServerBinding = AtServerBinding<
	AtOn<"+TEMP", &SizeTemp>,
	AtOn<"+LED", &SizeLed>,
	AtOn<"+CWMODE", &SizeModeRead>,
	AtOn<"+CWMODE", &SizeModeWrite>,
	AtOn<"+JOIN", &SizeJoin>,
	AtOn<"+RUN", &SizeRun>
>;


void SizeServerSession(SizeApp& app, Print& out, uint8 byte) {
	AtServer<SizeSet, SizeApp, SizeServerBinding> server(app, out);
	server.Feed(byte);
	server.Process();
	server.Urc<"WIFI GOT IP">();
}
