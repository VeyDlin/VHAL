#include "SizeEspAt.h"


// The ESP-AT client of SizeEspAtRows.cpp with the await module as well. Its session makes the same plain calls, and then the Await calls
// of each form, so the difference to the other object is the Await code.


using SizeEspAtAwaitClient = AtClient<EspAt, SizeEspAtBinding, AtQueue<2>, AtEcho, AtRows<8>, AtAwait>;


void SizeEspAtAwaitSession(Print& out, uint8 byte) {
	SizeEspAtAwaitClient client(out);
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
	static_cast<void>(client.AwaitRead<"+CWMODE">());
	static_cast<void>(client.AwaitRun<"+CIFSR">());
	static_cast<void>(client.AwaitWrite<"+CWMODE">(uint8{1}, std::optional<bool>()));
	static_cast<void>(client.AwaitRows<"+CWLAP">());
	static_cast<void>(client.AwaitSendData<"+CIPSEND">(uint16{5}, std::span<const uint8>(SizeEspAtData)));
	static_cast<void>(client.AwaitValue<"E">(uint8{0}));
}
