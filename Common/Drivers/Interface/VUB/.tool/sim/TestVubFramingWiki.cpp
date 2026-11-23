#include "SimCheck.h"
#include <Drivers/Interface/VUB/Framing/VubCobs.h>
#include <Drivers/Interface/VUB/Framing/VubGap.h>
#include <Drivers/Interface/VUB/Framing/VubLengthPrefix.h>
#include <Drivers/Interface/VUB/Vub.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <array>
#include <cstddef>
#include <span>


// The snippets of the framing pages of the wiki. Each one is the text of its page, and each compiles and runs here.


namespace CobsSnippet {
struct Uart {
	static constexpr VubLinkTraits traits = VubLinkTraits().MaxPacket(64).FullDuplex();


	Result<std::size_t> Write(std::span<const uint8> bytes) {
		return bytes.size();
	}
};


void OnPacket(const VubMeta&, std::span<const uint8> payload) {
	(void)payload;
}


using Stack = Vub<+[] {
	return VubBuilder()
		.Framing<VubCobs>()
		.Link<Uart>()
		.MaxPayload(64)
		.OnReceive<&OnPacket>();
}>;


Uart uart;
Stack stack(uart);


void Run() {
	const std::array<uint8, 3> hello = {0x11, 0x00, 0x22};
	stack.Send(std::span<const uint8>(hello.data(), hello.size()));
	stack.Process();
}
}


namespace LengthSnippet {
struct Uart {
	static constexpr VubLinkTraits traits = VubLinkTraits().MaxPacket(64).FullDuplex();


	Result<std::size_t> Write(std::span<const uint8> bytes) {
		return bytes.size();
	}
};


void OnPacket(const VubMeta&, std::span<const uint8> payload) {
	(void)payload;
}


using Stack = Vub<+[] {
	return VubBuilder()
		.Framing<VubLengthPrefix<1>>()
		.Link<Uart>()
		.MaxPayload(64)
		.OnReceive<&OnPacket>();
}>;


Uart uart;
Stack stack(uart);


void Run() {
	const std::array<uint8, 3> hello = {0x11, 0x22, 0x33};
	stack.Send(std::span<const uint8>(hello.data(), hello.size()));
	stack.Process();
}
}


namespace GapSnippet {
struct Uart {
	static constexpr VubLinkTraits traits = VubLinkTraits().MaxPacket(64).FullDuplex();


	Result<std::size_t> Write(std::span<const uint8> bytes) {
		return bytes.size();
	}
};


void OnPacket(const VubMeta&, std::span<const uint8> payload) {
	(void)payload;
}


// The user's microsecond clock. A timer counter in a real device; a constant here.
uint32 MicrosecondClock() {
	return 0;
}


using Stack = Vub<+[] {
	return VubBuilder()
		.Framing<VubGap>()
		.GapCharacters(35, 9600)
		.Link<Uart>()
		.MaxPayload(64)
		.OnReceive<&OnPacket>()
		.Clock<&MicrosecondClock>();
}>;


Uart uart;
Stack stack(uart);


// The receive interrupt of the UART calls this for each byte.
void OnUartByte(uint8 byte) {
	stack.Feed(byte);
}


// After the UART's parameters change, the framing is given the rate the UART is set to.
void OnBaudChanged(uint32 baud) {
	stack.Find<VubGap>().SetBaud(baud);
}


void Run() {
	const std::array<uint8, 2> hello = {0x11, 0x22};
	stack.Send(std::span<const uint8>(hello.data(), hello.size()));
	stack.Process();
}
}


// Growth is a compile-time check as well as a value, so the wiki's growth figures are checked in constant expressions.
static_assert(VubCobs::Growth(3) == 5);
static_assert(VubLengthPrefix<1>::Growth(3) == 3);
static_assert(VubGap::Growth(3) == 0);


void TestCobsSnippet() {
	CobsSnippet::Run();
	SIM_EXPECT(CobsSnippet::stack.Drops().framingError == 0);
}


void TestLengthSnippet() {
	LengthSnippet::Run();
	SIM_EXPECT(LengthSnippet::stack.Drops().framingError == 0);
}


void TestGapSnippet() {
	GapSnippet::OnBaudChanged(19200);
	GapSnippet::OnUartByte(0x55);
	GapSnippet::Run();
	SIM_EXPECT(GapSnippet::stack.Drops().framingError == 0);
	SIM_EXPECT(GapSnippet::stack.Find<VubGap>().SetBaud(9600) == ResultStatus::ok);
}


int main() {
	TestCobsSnippet();
	TestLengthSnippet();
	TestGapSnippet();
	return SimFinish("TestVubFramingWiki");
}
