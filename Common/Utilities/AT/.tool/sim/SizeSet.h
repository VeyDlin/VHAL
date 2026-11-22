#pragma once
#include <Utilities/AT/AtCommandSet.h>
#include <Utilities/AT/AtError.h>


// The description of the size check: five commands, with a read, a write, a test, a run and an optional string, and one URC. The server
// and the client objects are built from it separately, so each object's size is its own side's code.
using SizeSet = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Defaults(AtDefaults().TimeoutMs(2000))
		.Command<"+TEMP">().Read<int16>("celsius")
		.Command<"+LED">().Write(Argument<bool>("on"))
		.Command<"+CWMODE">().Test().Read<int8>("mode").Write(Argument<int8>("mode"))
		.Command<"+JOIN">().Write(Argument<String<32>>("ssid"), Optional<String<64>>("password")).TimeoutMs(15000)
		.Command<"+RUN">().Run()
		.Urc<"WIFI GOT IP">();
}>;
