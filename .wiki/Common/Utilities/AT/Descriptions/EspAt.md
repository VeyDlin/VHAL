# EspAt

`Common/Utilities/AT/Descriptions/EspAt.h`: the ESP-AT description. It lists the commands that `SocketAdapter` and `WiFiAdapter` need, and the URCs of ESP-AT. One description serves the client and the server, as for any other set.

**Status:** the description, its transcripts and its host tests are done. The forms, the fields, the ranges and the final lines come from the ESP-AT command reference. The check against a flashed ESP-AT release is done on the bench, before release. Every value marked "checked on the bench" below is a value to confirm there.

---

## Table of Contents

- [Reference](#reference)
- [Declaration](#declaration)
- [Structs](#structs)
- [Commands](#commands)
- [URCs](#urcs)
- [Final Lines](#final-lines)
- [Timeouts](#timeouts)
- [Not Declared](#not-declared)
- [Bench Check](#bench-check)
- [Tests](#tests)

---

## Reference

- ESP-AT command reference for the ESP32, read from the official pages (Basic AT commands, Wi-Fi AT commands, TCP/IP AT commands and the command index).
- Source commit `b2921f8f`, read 2026-11-22. The release on the bench may differ. Its `AT+GMR` text is not recorded on this page yet, and is checked in [Bench Check](#bench-check).
- Each command's comment in `EspAt.h` names its reference section.

Three places differ from the first design, and the reference decides them:

- `AT+CWJAP` fails with `+CWJAP:<code>` and then `ERROR`. It has no `FAIL` final.
- `AT+CIFSR` writes `+CIFSR:STAIP,"<ip>"`, with a comma after its prefix.
- `AT+GMR` writes its version lines unquoted, so its value is a `RawString<64>`.

---

## Declaration

```cpp
#include <Utilities/AT/Descriptions/EspAt.h>

// The client, with echo skipping, rows of 8 and the await calls. ClientBinding holds the URC handlers of the application.
AtClient<EspAt, ClientBinding, AtQueue<2>, AtEcho, AtRows<8>, AtAwait> modem(out);

// The server, with the handlers of the application for every form except the prompt of AT+CIPSEND
AtServer<EspAt, App, ServerBinding> server(app, out);
```

The header holds the description and the two structs. The bindings are the application's. `TestAtEspAt.cpp` has a complete server binding and a client binding as examples.

The set holds `AtDataMode` (for the `AT+CIPSEND` prompt and the `+IPD` data), and the dialect has `.Echo()`. The dialect's `ATE` is then a builtin name, so the client sends `ATE0` with `Value<"E", &H>(uint8{0})`.

On the server, the prompt command `AT+CIPSEND` has no handler. Its line is answered with `ERROR`. Every other form needs a handler.

---

## Structs

Both are declared in `EspAt.h`, next to the description.

```cpp
struct EspAtAccessPoint {       // one row of AT+CWLAP
	uint8 ecn = 0;
	String<32> ssid{};
	int8 rssi = 0;
	String<17> mac{};
	uint8 channel = 0;
};

struct EspAtJoinedNetwork {     // the record of AT+CWJAP?
	String<32> ssid{};
	String<17> bssid{};
	uint8 channel = 0;
	int8 rssi = 0;
};
```

The client skips the fields after the declared ones. For `AT+CWLAP` that is `freq_offset` onward. For `AT+CWJAP?` it is `pci_en` onward.

---

## Commands

The handler columns show the client's handler (the template argument of the call) and the server's handler (the bound function). `AtCall<App>` is the first argument of every server handler.

| Command | Forms | Client call and handler | Server handler | Timeout | Reference section |
|---|---|---|---|---|---|
| `AT+GMR` | run, value `RawString<64>`, prefix `AT version:` | `Run<"+GMR", &H>()`, `void H(Result<RawString<64>, AtStatus>)` | `Result<RawString<64>, AtStatus> N(AtCall<App>)` | default | AT+GMR |
| `AT+RST` | run | `Run<"+RST", &H>()`, `void H(AtStatus)` | `AtStatus N(AtCall<App>)` | default | AT+RST |
| `AT+CWMODE` | read `uint8 mode`; write `mode` 0 to 3, `autoConnect` optional bool | `Read<"+CWMODE", &H>()`, `void H(Result<uint8, AtStatus>)`; `Write<"+CWMODE", &H>(...)`, `void H(AtStatus)` | `Result<uint8, AtStatus> N(AtCall<App>)`; `AtStatus N(AtCall<App>, uint8, std::optional<bool>)` | default | AT+CWMODE |
| `AT+CWJAP` | read record `EspAtJoinedNetwork`; write `ssid` String<32>, `password` String<64>, `bssid` optional String<17> | `Read<"+CWJAP", &H>()`, `void H(Result<EspAtJoinedNetwork, AtStatus>)`; `Write<"+CWJAP", &H>(...)`, `void H(AtStatus)` | `Result<EspAtJoinedNetwork, AtStatus> N(AtCall<App>)`; `AtStatus N(AtCall<App>, string_view, string_view, optional<string_view>)` | 20000 (see Timeouts) | AT+CWJAP |
| `AT+CWQAP` | run | `Run<"+CWQAP", &H>()`, `void H(AtStatus)` | `AtStatus N(AtCall<App>)` | default | AT+CWQAP |
| `AT+CWLAP` | run rows `EspAtAccessPoint`, with `(` and `)` | `Rows<"+CWLAP", &H>()`, `void H(const Result<AtRowList<EspAtAccessPoint, N>, AtStatus>&)` | `AtStatus N(AtCall<App>, AtRowWriter<EspAtAccessPoint>&)` | 10000 | AT+CWLAP |
| `AT+CIFSR` | run value `String<15>`, prefix `+CIFSR:STAIP` | `Run<"+CIFSR", &H>()`, `void H(Result<String<15>, AtStatus>)` | `Result<String<15>, AtStatus> N(AtCall<App>)` | default | AT+CIFSR |
| `AT+CIPMUX` | read `uint8 mode`; write `mode` 0 to 1 | `Read<"+CIPMUX", &H>()`, `void H(Result<uint8, AtStatus>)`; `Write<"+CIPMUX", &H>(...)` | `Result<uint8, AtStatus> N(AtCall<App>)`; `AtStatus N(AtCall<App>, uint8)` | default | AT+CIPMUX |
| `AT+CIPSTART` | write `type` String<8>, `host` String<64>, `port` uint16, `keepAlive` optional uint16 0 to 7200 | `Write<"+CIPSTART", &H>(...)`, `void H(AtStatus)` | `AtStatus N(AtCall<App>, string_view, string_view, uint16, optional<uint16>)` | 15000 | AT+CIPSTART |
| `AT+CIPSEND` | write `length` uint16 1 to 8192, `.Prompt()`, finals `SEND OK`, failures `SEND FAIL` | `SendData<"+CIPSEND", &H>(uint16, span)`, `void H(AtStatus)` | none: the prompt is answered with `ERROR` | 15000 | AT+CIPSEND |
| `AT+CIPCLOSE` | run | `Run<"+CIPCLOSE", &H>()`, `void H(AtStatus)` | `AtStatus N(AtCall<App>)` | default | AT+CIPCLOSE |
| `ATE` | builtin from `.Echo()` | `Value<"E", &H>(uint8{0})`, `void H(AtStatus)` | `ATE0` and `ATE1` with the dialect | default | ATE |

Notes:

- `AT+CWJAP` has no test form and `AT+CWMODE` has no test form. The reference does not state one.
- `AT+CWLAP` has no read form, so it has no `ReadRows`. Its set form is not declared.
- `AT+CIPSEND` declares the length range from the reference: 1 to 8192. The client's `SendData` keeps the data span, which must stay valid until the handler runs.
- The `N` on `AtRowList` is the client's `AtRows<N>` capacity. The description does not fix it.

---

## URCs

| Prefix | Arguments | Client handler |
|---|---|---|
| `ready` | none | `void H()` |
| `busy p...` | none | `void H()` |
| `WIFI CONNECTED` | none | `void H()` |
| `WIFI GOT IP` | none | `void H()` |
| `WIFI DISCONNECT` | none | `void H()` |
| `CONNECT` | none | `void H()` |
| `CLOSED` | none | `void H()` |
| `+IPD` | `length` uint16, then `Data()` | `void H(uint16 length, uint16 offset, std::span<const uint8> chunk, bool last)` |

- The `+IPD` line is `+IPD,<length>:<data>` with `CIPMUX=0`. The data is raw, so CR and LF in it are data, and the chunks come in order (see [AtDataMode](../AtDataMode.md)).
- `WIFI GOT IPv6 LL` and `WIFI GOT IPv6 GL` are not declared. Their prefix does not end at `IP`, so they are not `WIFI GOT IP` lines.
- The server queues the URCs without `Data()`, for example `server.Urc<"WIFI GOT IP">()`. A server cannot queue `+IPD`.

---

## Final Lines

| Line | Status on the client |
|---|---|
| `OK` | `ok` |
| `ERROR` | `error` |
| `+CME ERROR:` | `error`, with its code when the dialect has the text |
| `SEND OK` (declared final of `AT+CIPSEND`) | `ok` |
| `SEND FAIL` (declared failure of `AT+CIPSEND`) | `error` |

`FAIL` is not a final of any command in the set. The reference does not give one.

### Failure Codes

`AT+CWJAP` declares `.FailureCode("+CWJAP")`. The `+CWJAP:<code>` line before a failed join is read as the code, and the status is `error` with `failureCode` set to it (see [AtClient](../AtClient.md#failure-codes)). The codes are named by `EspAtJoinFailure`:

| Code | `EspAtJoinFailure` | Cause in the reference |
|---|---|---|
| 1 | `timeout` | connection timeout |
| 2 | `wrongPassword` | wrong password |
| 3 | `noAccessPoint` | the access point cannot be found |
| 4 | `connectionFailed` | the connection failed |

The reference gives these codes "for reference only". Each one is checked on the bench (see [Bench Check](#bench-check)).

```cpp
void OnJoin(AtStatus status) {
	if (status.failureCode == static_cast<uint8>(EspAtJoinFailure::wrongPassword)) {
		// the password is wrong
	}
}
```

---

## Timeouts

The description's default is 2000 ms. A command can override it with `.TimeoutMs()`.

| Command | Timeout | Where it comes from |
|---|---|---|
| `AT+CWJAP` | 20000 ms | The reference gives `jap_timeout`, default 15 s, range 3 to 600 s. The value is above the default. Checked on the bench. |
| `AT+CWLAP` | 10000 ms | Not stated. Chosen. Checked on the bench. |
| `AT+CIPSTART` | 15000 ms | Not stated for the connection. Chosen. Checked on the bench. |
| `AT+CIPSEND` | 15000 ms | Not stated. Chosen. It covers the prompt, the data, `Recv` and `SEND OK`. Checked on the bench. |
| all others | 2000 ms | The default. Not stated by the reference. Checked on the bench. |

A timeout starts the drain: the client sends `AT`, and the drain ends by the rule in [AtClient](../AtClient.md#timing-queue-and-drain).

---

## Not Declared

These lines and forms are not in the description. Each one is a line the client ignores, or a form it does not send.

- `AT+CWMODE=?`, `AT+CWJAP=?`, `AT+CWLAP=?` and the other test forms. The reference does not state them.
- `AT+CWLAP` set form, with its ssid, mac and channel filters. Its response is not stated.
- `AT+CWLAPOPT`, `AT+CWHOSTNAME`, `AT+CIPSTATUS`, `AT+CIPSTO`, `AT+CWSAP`, `AT+CWDHCP`, `AT+CIPSERVER`, `AT+PING`, and the other commands of the reference.
- `AT+CIPMUX=1` forms: `AT+CIPSTART=<id>,...`, `AT+CIPSEND=<id>,<length>`, `AT+CIPCLOSE=<id>`, and `+IPD,<id>,<length>:`. Multiple connections are the second description step.
- The optional `local IP` and `timeout` fields of `AT+CIPSTART`.
- The optional fields of `AT+CWJAP` after `bssid`: `authmode`, `reconn_interval`, `listen_interval`, `scan_mode`, `jap_timeout` and `pmf`.
- URCs `WIFI GOT IPv6 LL`, `WIFI GOT IPv6 GL`, `Will force to restart!!!`, `SEND Canceled`, `Recv <n> bytes`, and the `ERR CODE:0x...` lines of `AT+SYSLOG`.
- `AT+CIPDINFO`: the `+IPD` line with the remote address and port.

---

## Bench Check

The check runs on an ESP32 with the flashed release. Each item below is checked, and the result is recorded on this page.

1. The `AT+GMR` version text, which is the release's text for `AT version:`. The `RawString<64>` must hold it.
2. The `ready` line after `AT+RST`, and whether `AT+RST` answers `OK` first.
3. The echo default (`ATE1` at start).
4. `AT+CWJAP` with a wrong password: `+CWJAP:<code>` then `ERROR`, not `FAIL`. The client must return `error`, not `timeout`, with `failureCode` 2. Each code of `EspAtJoinFailure` (1 to 4) is checked with its cause: a timeout, a wrong password, no access point, and a failed connection.
5. `AT+CWJAP` with a right password: the `WIFI CONNECTED` and `WIFI GOT IP` lines, and whether the `WIFI GOT IP` URC reaches its handler while the command is open.
6. `AT+CWLAP`: the row format, and the time a scan takes against the 10000 ms timeout.
7. `AT+CIFSR`: the `+CIFSR:STAIP,"<ip>"` line, with its comma.
8. `AT+CIPSTART`: the `CONNECT` line before `OK`, and the time it takes against the 15000 ms timeout.
9. `AT+CIPSEND`: the `OK` then `>` sequence, the `Recv <n> bytes` line, `SEND OK`, and the time against the 15000 ms timeout. Also a `SEND FAIL` case.
10. `+IPD` during a send, and during an `AT+CWLAP` scan.
11. The `busy p...` line while a previous command runs.
12. The drain after a real timeout: the module's answer to `AT` after a lost request.
13. Each timeout on this page, with the time the module takes at worst.

---

## Tests

`Common/Utilities/AT/.tool/sim/TestAtEspAt.cpp` builds one client with `AtEcho`, `AtRows<8>` and `AtAwait`, and one server from the description. Both must compile. The server binds every form except the prompt. The test checks the server's answers on the text they must have.

The client runs these transcripts, in `.tool/sim/transcripts/espat_*.txt`. They are written by hand from the reference, and each one says so in a comment line. Each is fed whole, cut at every byte, and one byte at a time.

| Transcript | What it covers |
|---|---|
| `espat_boot.txt` | `ready` at start, and `WIFI DISCONNECT` while idle |
| `espat_ate0.txt` | `ATE0` with its echo |
| `espat_gmr.txt` | `AT+GMR` with unquoted lines and a blank before `OK` |
| `espat_cwmode.txt` | `AT+CWMODE?` and `AT+CWMODE=1` |
| `espat_busy.txt` | `busy p...` between the echo and the reply |
| `espat_cwjap_ok.txt` | `WIFI CONNECTED` and `WIFI GOT IP` inside the reply, and the `IPv6` line |
| `espat_cwjap_fail.txt` | `+CWJAP:2` then `ERROR`: status `error` with `failureCode` 2 |
| `espat_cwjap_query.txt` | `AT+CWJAP?` as a record, with the fields after the rssi skipped |
| `espat_cwlap.txt` | four rows, a URC between two rows, a quoted comma in an ssid |
| `espat_cwlap_bad_row.txt` | a row with a missing field: the reply is `invalidData` |
| `espat_cifsr.txt` | `AT+CIFSR` with the `STAIP` line among the other address lines |
| `espat_cipmux.txt` | `AT+CIPMUX?` |
| `espat_cipstart.txt` | `CONNECT` as a URC inside the reply |
| `espat_cipsend.txt` | `OK`, the `>` prompt, the five bytes, `Recv 5 bytes`, `SEND OK` |
| `espat_cipsend_ipd.txt` | a `+IPD` with three bytes between the data and `SEND OK` |
| `espat_cipclose.txt` | `CLOSED` as a URC inside the reply |
| `espat_timeout_then_at.txt` | a silent request, its timeout, and the drain's `AT` |
| `espat_late_reply_then_drain.txt` | a late reply after the timeout, the drain, then the next request |

The host test has 2456 checks (`TestAtEspAt: all 2456 checks passed`).

### Sizes

The client objects at -Os for Cortex-M4 (from `run.sh`). Each holds the same calls, except the rows call, which needs `AtRows`, and the await calls, which need `AtAwait`.

| Object | text | data | bss |
|---|---|---|---|
| client with `AtEcho` and `AtRows<8>` | 11237 | 4 | 32 |
| the same with `AtAwait` | 12979 | 4 | 32 |
| client with neither module | 9551 | 4 | 32 |

- `AtRows<8>` and `AtEcho` add 1686 bytes of text to the plain client.
- `AtAwait` adds 1742 bytes of text to the client with rows.
- The `AT+CWJAP` failure code line adds 179 bytes of text to the plain client: the line parser, a prefix table with one entry, and the status step that keeps the code. Without that line the plain client is 9372 bytes.
- The client object itself, on the stack or in the application's storage, is 1160 bytes for the plain client, 1752 bytes with `AtEcho` and `AtRows<8>`, and 2792 bytes with `AtAwait` as well. The difference is the queue, whose entries hold the whole row storage with `AtAwait`.
