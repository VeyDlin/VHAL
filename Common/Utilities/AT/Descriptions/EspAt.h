#pragma once
#include <Utilities/AT/AtCommandSet.h>
#include <Utilities/AT/AtRow.h>
#include <Utilities/AT/AtValue.h>
#include <Utilities/AT/Modules/AtDataMode.h>
#include <Utilities/CommandLine/CommandTypes.h>
#include <Utilities/DataTypes.h>


// The ESP-AT description: the commands that SocketAdapter and WiFiAdapter need, and the URCs of ESP-AT. The forms, the fields, the ranges
// and the final lines follow the ESP-AT command reference extract (ESP32, source commit b2921f8f). The extract is the reference for each
// comment below, which starts with its section, such as AT+CWMODE. A timeout that the reference does not state is a value chosen
// here. Each one is listed in the wiki as checked on the bench before release.
//
// ESP-AT ends a failed AT+CWJAP with +CWJAP:<code> and then ERROR, so AT+CWJAP has no failure final. FAIL is not a final line of any
// command in the reference. SEND FAIL ends AT+CIPSEND. The reference says the stock firmware echoes, so the dialect declares ATE. The set
// has AtDataMode for the prompt of AT+CIPSEND and for the +IPD data.


// One access point of AT+CWLAP, from its info line +CWLAP:(<ecn>,<"ssid">,<rssi>,<"mac">,<channel>,...). The fields after channel are
// skipped by the client.
struct EspAtAccessPoint {
	uint8 ecn = 0;
	String<32> ssid{};
	int8 rssi = 0;
	String<17> mac{};
	uint8 channel = 0;
};


// The network that AT+CWJAP? reports, from its info line +CWJAP:<"ssid">,<"bssid">,<channel>,<rssi>,... The fields after rssi are
// skipped by the client.
struct EspAtJoinedNetwork {
	String<32> ssid{};
	String<17> bssid{};
	uint8 channel = 0;
	int8 rssi = 0;
};


// The ESP-AT description. The chain is a captureless lambda, as in the other AT sets.
using EspAt = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Defaults(AtDefaults().TimeoutMs(2000))
		.Modules(AtDataMode)
		.Dialect(AtDialect().Echo())
		// AT+GMR, execute only. The version lines are unquoted text, so the value is the rest of the line. Other lines are skipped.
		.Command<"+GMR">().Run<RawString<64>>("version").Reply("AT version:")
		// AT+RST, execute only. The module answers OK, then restarts and sends ready as a URC.
		.Command<"+RST">().Run()
		// AT+CWMODE, query and set. The mode is 0 to 3, and the optional auto-connect flag is 0 or 1.
		.Command<"+CWMODE">()
		.Read<uint8>("mode")
		.Write(Argument<uint8>("mode").Range(0, 3), Optional<bool>("autoConnect"))
		// AT+CWJAP, query and set. The query gives one record. The set has a 20 second timeout, above the reference default of 15 seconds
		// for jap_timeout. A failure is +CWJAP:<code> and ERROR.
		.Command<"+CWJAP">()
		.Read(AtRow<EspAtJoinedNetwork>(
			AtField<&EspAtJoinedNetwork::ssid>("ssid"),
			AtField<&EspAtJoinedNetwork::bssid>("bssid"),
			AtField<&EspAtJoinedNetwork::channel>("channel"),
			AtField<&EspAtJoinedNetwork::rssi>("rssi")))
		.Write(Argument<String<32>>("ssid"), Argument<String<64>>("password"), Optional<String<17>>("bssid"))
		.TimeoutMs(20000)
		// AT+CWQAP, execute only.
		.Command<"+CWQAP">().Run()
		// AT+CWLAP, execute only. One access point for each info line, in the parentheses. The ecn field is 0 to 15. Its timeout is
		// chosen here: the reference does not state one for a scan.
		.Command<"+CWLAP">()
		.Rows(AtRow<EspAtAccessPoint>(
			AtField<&EspAtAccessPoint::ecn>("ecn").Range(0, 15),
			AtField<&EspAtAccessPoint::ssid>("ssid"),
			AtField<&EspAtAccessPoint::rssi>("rssi"),
			AtField<&EspAtAccessPoint::mac>("mac"),
			AtField<&EspAtAccessPoint::channel>("channel"))
			.Open("(")
			.Close(")"))
		.TimeoutMs(10000)
		// AT+CIFSR, execute only. The station address is the line +CIFSR:STAIP,"<ip>". The other address lines are skipped.
		.Command<"+CIFSR">().Run<String<15>>("ip").Reply("+CIFSR:STAIP")
		// AT+CIPMUX, query and set. The mode is 0 (single connection) or 1 (multiple connections).
		.Command<"+CIPMUX">()
		.Read<uint8>("mode")
		.Write(Argument<uint8>("mode").Range(0, 1))
		// AT+CIPSTART, set, single connection. The keep-alive is 0 to 7200 seconds. The timeout is chosen here, since the reference does
		// not state one for the connection.
		.Command<"+CIPSTART">()
		.Write(
			Argument<String<8>>("type"),
			Argument<String<64>>("host"),
			Argument<uint16>("port"),
			Optional<uint16>("keepAlive").Range(0, 7200))
		.TimeoutMs(15000)
		// AT+CIPSEND, set, single connection. OK is followed by the '>' prompt, and the data is sent after it. SEND OK and SEND FAIL end
		// the request. The length is 1 to 8192. The timeout covers the whole exchange and is chosen here.
		.Command<"+CIPSEND">()
		.Write(Argument<uint16>("length").Range(1, 8192))
		.Prompt()
		.Finals("SEND OK")
		.Failures("SEND FAIL")
		.TimeoutMs(15000)
		// AT+CIPCLOSE, execute only, single connection. The module sends CLOSED, which is a URC, and then OK.
		.Command<"+CIPCLOSE">().Run()
		// The URCs of ESP-AT: the module's ready message, the Wi-Fi and TCP events, and the +IPD data with its length.
		.Urc<"ready">()
		.Urc<"busy p...">()
		.Urc<"WIFI CONNECTED">()
		.Urc<"WIFI GOT IP">()
		.Urc<"WIFI DISCONNECT">()
		.Urc<"CONNECT">()
		.Urc<"CLOSED">()
		.Urc<"+IPD">(Argument<uint16>("length"), Data());
}>;
