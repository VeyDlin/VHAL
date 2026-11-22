// The scale set: about 20 ESP-AT-like commands with test, read, write and run forms, two basic commands, options and URCs. It is
// compiled by run.sh with the host g++ and with the target compiler (arm-none-eabi-g++, -fsyntax-only), to check the compile time and
// the size of the description at the scale of the first ESP-AT description. It is not a test: the tests are the Test*.cpp files.
#include <Utilities/AT/AtCommandSet.h>
#include <Utilities/AT/Modules/AtDataMode.h>

using EspAt = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Defaults(AtDefaults().TimeoutMs(2000))
		.Modules(AtDataMode)
		.Dialect(AtDialect().Echo().Verbose().Quiet().CmeCommand().Registers())
		.Basic<"Z">().Value(Argument<uint8>("on"))
		.Basic<"I">().Run()
		.Command<"+GMR">().Run()
		.Command<"+RST">().Run()
		.Command<"+CWMODE">().Test().Read<int8>("mode").Write(Argument<int8>("mode"))
		.Command<"+CWJAP">().Write(Argument<String<32>>("ssid"), Optional<String<64>>("password")).TimeoutMs(15000).Failures("FAIL")
		.Command<"+CWQAP">().Run()
		.Command<"+CWLAP">().Run()
		.Command<"+CIFSR">().Run().Reply("+CIFSR:STAIP")
		.Command<"+CIPMUX">().Test().Read<uint8>("mode").Write(Argument<uint8>("mode"))
		.Command<"+CIPSTART">().Write(Argument<String<8>>("type"), Argument<String<64>>("host"), Argument<uint16>("port")).TimeoutMs(5000)
		.Command<"+CIPSEND">().Write(Argument<uint16>("length")).Finals("SEND OK").Failures("SEND FAIL")
		.Command<"+CIPCLOSE">().Run()
		.Command<"+CIPSTATUS">().Run()
		.Command<"+CIPSTO">().Read<uint16>("seconds").Write(Argument<uint16>("seconds"))
		.Command<"+CWHOSTNAME">().Read<String<32>>("name").Write(Argument<String<32>>("name"))
		.Command<"+CWDHCP">().Write(Argument<uint8>("mode"), Argument<bool>("enable"))
		.Command<"+CIPSERVER">().Write(Argument<bool>("enable"), Optional<uint16>("port"))
		.Command<"+PING">().Write(Argument<String<64>>("host"))
		.Command<"+CWSAP">().Test().Read<String<32>>("ssid").Write(Argument<String<32>>("ssid"), Argument<String<64>>("password"), Argument<uint8>("channel"))
		.Command<"+CIPDNS">().Test().Read<String<32>>("servers").Write(Argument<bool>("enable"), Optional<String<32>>("dns"))
		.Urc<"WIFI CONNECTED">()
		.Urc<"WIFI GOT IP">()
		.Urc<"WIFI DISCONNECT">()
		.Urc<"CONNECT">()
		.Urc<"CLOSED">()
		.Urc<"+IPD">(Argument<uint16>("length"));
}>;

static_assert(EspAt::CommandCount == 21);
static_assert(EspAt::UrcCount == 6);
static_assert(EspAt::Index<"+CIPSEND">() == 11);
static_assert(EspAt::commands[11].options.finalCount == 1);
static_assert(EspAt::urcsChecked);

int main() {
	return 0;
}
