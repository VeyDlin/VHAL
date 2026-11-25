#include "SimCheck.h"
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/Framing/Gap.h>
#include <Drivers/Interface/VUB/Framing/LengthPrefix.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <array>
#include <cstddef>
#include <span>


// The snippets of the framing pages of the wiki. Each one is the text of its page, and each compiles and runs here.


namespace CobsSnippet {
struct Uart {
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().MaxPacket(64).FullDuplex();


	Result<std::size_t> Write(std::span<const uint8> bytes) {
		return bytes.size();
	}
};


void OnPacket(const VUB::Meta&, std::span<const uint8> payload) {
	(void)payload;
}


using Device = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Cobs>()
		.Link<Uart>()
		.MaxPayload(64)
		.OnReceive<&OnPacket>();
}>;


Uart uart;
Device device(uart);


void Run() {
	const std::array<uint8, 3> hello = {0x11, 0x00, 0x22};
	device.Send(std::span<const uint8>(hello.data(), hello.size()));
	device.Process();
}
}


namespace LengthSnippet {
struct Uart {
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().MaxPacket(64).FullDuplex();


	Result<std::size_t> Write(std::span<const uint8> bytes) {
		return bytes.size();
	}
};


void OnPacket(const VUB::Meta&, std::span<const uint8> payload) {
	(void)payload;
}


using Device = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::LengthPrefix<1>>()
		.Link<Uart>()
		.MaxPayload(64)
		.OnReceive<&OnPacket>();
}>;


Uart uart;
Device device(uart);


void Run() {
	const std::array<uint8, 3> hello = {0x11, 0x22, 0x33};
	device.Send(std::span<const uint8>(hello.data(), hello.size()));
	device.Process();
}
}


namespace GapSnippet {
struct Uart {
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().MaxPacket(64).FullDuplex();


	Result<std::size_t> Write(std::span<const uint8> bytes) {
		return bytes.size();
	}
};


void OnPacket(const VUB::Meta&, std::span<const uint8> payload) {
	(void)payload;
}


// The user's microsecond clock. A timer counter in a real device; a constant here.
uint32 MicrosecondClock() {
	return 0;
}


using Device = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Gap>()
		.GapCharacters(35, 9600)
		.Link<Uart>()
		.MaxPayload(64)
		.OnReceive<&OnPacket>()
		.Clock<&MicrosecondClock>();
}>;


Uart uart;
Device device(uart);


// The receive interrupt of the UART calls this for each byte.
void OnUartByte(uint8 byte) {
	device.Feed(byte);
}


// After the UART's parameters change, the framing gets the baud rate the UART is set to.
void OnBaudChanged(uint32 baud) {
	device.Find<VUB::Gap>().SetBaud(baud);
}


void Run() {
	const std::array<uint8, 2> hello = {0x11, 0x22};
	device.Send(std::span<const uint8>(hello.data(), hello.size()));
	device.Process();
}
}


// Growth is a compile-time check as well as a value, so the wiki's growth figures are checked in constant expressions.
static_assert(VUB::Cobs::Growth(3) == 2);
static_assert(VUB::LengthPrefix<1>::Growth(3) == 3);
static_assert(VUB::Gap::Growth(3) == 0);


void TestCobsSnippet() {
	CobsSnippet::Run();
	SIM_EXPECT(CobsSnippet::device.Drops().framingError == 0);
}


void TestLengthSnippet() {
	LengthSnippet::Run();
	SIM_EXPECT(LengthSnippet::device.Drops().framingError == 0);
}


void TestGapSnippet() {
	GapSnippet::OnBaudChanged(19200);
	GapSnippet::OnUartByte(0x55);
	GapSnippet::Run();
	SIM_EXPECT(GapSnippet::device.Drops().framingError == 0);
	SIM_EXPECT(GapSnippet::device.Find<VUB::Gap>().SetBaud(9600) == ResultStatus::ok);
}


int main() {
	TestCobsSnippet();
	TestLengthSnippet();
	TestGapSnippet();
	return SimFinish("TestVubFramingWiki");
}
