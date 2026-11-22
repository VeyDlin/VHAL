#include "SizeEspAt.h"


// The ESP-AT client with the echo module and a row list of 8 rows. The entry point has external linkage, so the client's code stays in
// the object file. The calls are the ones the other two size objects make, except the Await calls, which only the Await object makes.


using SizeEspAtRowsClient = AtClient<EspAt, SizeEspAtBinding, AtQueue<2>, AtEcho, AtRows<8>>;


void SizeEspAtRowsSession(Print& out, uint8 byte) {
	SizeEspAtRowsClient client(out);
	client.Feed(byte);
	client.Process(0);
	client.Read<"+CWMODE", &SizeEspAtMode>();
	client.Write<"+CWMODE", &SizeEspAtStatus>(uint8{1}, std::optional<bool>());
	client.Run<"+CIFSR", &SizeEspAtAddress>();
	client.Read<"+CWJAP", &SizeEspAtJoined>();
	client.Rows<"+CWLAP", &SizeEspAtScan>();
	client.SendData<"+CIPSEND", &SizeEspAtStatus>(uint16{5}, std::span<const uint8>(SizeEspAtData));
	client.Value<"E", &SizeEspAtStatus>(uint8{0});
	client.Process(0);
}
