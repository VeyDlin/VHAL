#include "SizeEspAt.h"


// The ESP-AT client with neither the echo module, nor the row list, nor the await module. It makes the same calls as SizeEspAtRows.cpp,
// except the rows call, which needs AtRows. The entry point has external linkage, so the client's code stays in the object file.


using SizeEspAtPlainClient = AtClient<EspAt, SizeEspAtBinding, AtQueue<2>>;


void SizeEspAtPlainSession(Print& out, uint8 byte) {
	SizeEspAtPlainClient client(out);
	client.Feed(byte);
	client.Process(0);
	client.Read<"+CWMODE", &SizeEspAtMode>();
	client.Write<"+CWMODE", &SizeEspAtStatus>(uint8{1}, std::optional<bool>());
	client.Run<"+CIFSR", &SizeEspAtAddress>();
	client.Read<"+CWJAP", &SizeEspAtJoined>();
	client.SendData<"+CIPSEND", &SizeEspAtStatus>(uint16{5}, std::span<const uint8>(SizeEspAtData));
	client.Value<"E", &SizeEspAtStatus>(uint8{0});
	client.Process(0);
}
