# AtRows: Records and Rows

`Common/Utilities/AT/AtRow.h` (fields, records and rows, and the server's row writer) and `Common/Utilities/AT/Modules/AtRows.h` (the client module `AtRows<N>` and the list `AtRowList`).

A record is one info line with fields, such as `+JOIN: "net",-40`. A row is one of the info lines of a list, such as `+CWLAP: (4,"net",-72)`. Both are declared with their fields, and the fields name the data members of the struct that the handler gets.

---

## Table of Contents

- [Declaring a Row](#declaring-a-row)
- [Field Types](#field-types)
- [Open, Close and Extra Fields](#open-close-and-extra-fields)
- [Rules Checked at Compile Time](#rules-checked-at-compile-time)
- [Records](#records)
- [Rows](#rows)
- [RowsVisit and ReadRowsVisit](#rowsvisit-and-readrowsvisit)
- [The Server Side](#the-server-side)
- [The Line on the Wire](#the-line-on-the-wire)
- [Costs](#costs)

---

## Declaring a Row

```cpp
struct Network {
	int8 ecn = 0;
	String<32> ssid{};
	int8 rssi = 0;
	String<17> mac{};
	uint8 channel = 0;
};

constexpr auto NetworkRow = AtRow<Network>(
	AtField<&Network::ecn>("ecn"),
	AtField<&Network::ssid>("ssid"),
	AtField<&Network::rssi>("rssi").Range(-100, 0),
	AtField<&Network::mac>("mac"),
	AtField<&Network::channel>("channel")
).Open("(").Close(")");
```

- `AtRow<S>(fields...)` gives the declaration. Each field is `AtField<&S::member>("name")`, and the member's type is the field's type.
- `.Open(text)` and `.Close(text)` are the texts around the row. Both are optional. An empty text is a compile error: leave the call out instead.
- The declaration is a constant, so it can be a `constexpr` variable, and the same row can serve several commands.

---

## Field Types

| Member type | On the line |
|---|---|
| `int8`, `int16`, `int32`, `int64` | signed decimal, with `.Range(low, high)` to narrow it |
| `uint8`, `uint16`, `uint32` | unsigned decimal, with `.Range()` |
| `bool` | `0` or `1` |
| `String<N>` | quoted text, `\"` and `\\` as escapes |
| `RawString<N>` | unquoted text, to the end of the row (or to the Close text). Only as the last field |
| `std::optional<T>` of one of the above | an optional field. It is absent when empty, and it comes last |

- A field's range is the type's range until `.Range(low, high)` sets it. The reader refuses a value outside the range, and the row is not a row.
- A `RawString` ends where the row does: at its Close text, or at the end of the line. Its quotes are text, and a comma is text.

---

## Open, Close and Extra Fields

- A line must start with the Open text, after its reply prefix, and a row is complete at its Close text.
- Fields are separated by commas outside quotes. A `"` starts a quoted part, in which a comma, a Close text and a `)` are text.
- A field that is more than the declared fields is skipped to the end of the row, honouring quotes, so `(4,"a),b",-72,6,0)` with three fields reads `"a),b"` and skips `6,0`.
- A missing required field makes the line not a row. The reply is then `invalidData`, since the line is not a row of its command. Rows already read are not handed over.
- An empty optional field is absent.

---

## Rules Checked at Compile Time

Each rule is a compile error with its message. The snippets are in `Common/Utilities/AT/.tool/sim/BuilderErrors/`.

- a row has at least one field;
- a field's member belongs to the row's struct, and its type is one the grammar carries;
- a field has a name, and no two fields of a row have the same name;
- a required field does not follow an optional one;
- a `RawString` is the last field;
- `Range()` is only for an integer field, and its bounds fit the type, with low at most high;
- `Open` and `Close` are not empty.

A `Read(row)` or `ReadRows(row)` on a basic command is refused, as a `Read<T>` is.

---

## Records

```cpp
// AT+JOINED? answers +JOINED: "net",-40,"pw" (or +JOINED: "net",-40 when there is no password)
Result<Join, AtStatus> JoinedRead(AtCall<App> call);

// The description:
//   .Command<"+JOINED">().Read(JoinRow)
// The client:
//   client.Read<"+JOINED", &OnJoined>();   // void OnJoined(Result<Join, AtStatus>)
```

- `.Read(row)` is the read form, and `.Run(row)` is the run form, each with one info line of the record.
- The client gets `void(Result<S, AtStatus>)`, the same as a `Read<T>` with T the row type. `client.Read<"+X">` and `client.Run<"+X">` take the handler.
- The server's handler is `Result<S, AtStatus> Name(AtCall<App>)`. The server writes one info line from the row, and an error status is the final line.
- A command has one read form and one run form, so `.Read(row)` with `.Run<T>()` on the same command is allowed, and `.Read(row)` with `.ReadRows(row)` is refused.

---

## Rows

```cpp
// The description:
//   .Command<"+LIST">().Rows(NetworkRow)       // AT+LIST:      one info line for each row
//   .Command<"+SCAN">().ReadRows(NetworkRow)   // AT+SCAN?:     one info line for each row

// The client, with the row module in its list:
using Client = AtClient<Set, Binding, AtQueue<2>, AtEcho, AtRows<8>>;
void OnList(const Result<AtRowList<Network, 8>, AtStatus>& result) {
	if (result.IsOk()) {
		const AtRowList<Network, 8>& list = result.Value();

		for (const Network& network : list) {
			// network.ssid, network.rssi, ...
		}

		// list.Count() rows were stored; list.Skipped() rows came past the capacity and were not stored
	}
}
client.Rows<"+LIST", &OnList>();
client.ReadRows<"+SCAN", &OnList>();
```

- `Rows` and `ReadRows` need `AtRows<N>` in the client's modules. Without it, the call is a compile error that names `AtRows`.
- The handler is `void(const Result<AtRowList<S, N>, AtStatus>&)`. `N` is the module's capacity.
- `AtRowList` has `Count()`, `Skipped()`, `operator[]` (below `Count()`), and `begin()` and `end()` for a range-based loop.
- A row past the capacity is counted in `Skipped()`, and the status stays `ok`. The first N rows are kept.
- A row that does not parse makes the whole reply `invalidData`, and the handler gets the error.
- The list views the client's storage, so it is valid while the handler runs. The next request reuses it.
- The storage is one list of N rows, sized to the largest row type of the set. A client with no `AtRows` has no row storage.

---

## RowsVisit and ReadRowsVisit

```cpp
void OnRow(const Network& network) {
	// called for each row, as it arrives
}

void OnRowsDone(AtStatus status) {
	// called once, at the final line: ok, error, timeout, or invalidData for a row that did not parse
}

client.RowsVisit<"+LIST", &OnRow, &OnRowsDone>();
client.ReadRowsVisit<"+SCAN", &OnRow, &OnRowsDone>();
```

- Each row is handed to `OnRow` when its line arrives, so the handler may act before the list is complete.
- Nothing is stored, so there is no capacity and no `AtRows` module.
- A row that does not parse makes `OnDone` get `invalidData`. The rows before it were already handed over.

---

## The Server Side

```cpp
// The description entry: .Command<"+LIST">().Rows(NetworkRow)
AtStatus ListRun(AtCall<App> call, AtRowWriter<Network>& out) {
	for (const Network& network : call.app.networks) {
		out.Add(network);       // writes one info line now
	}

	return AtStatus{};          // a status other than ok ends the line with an error
}
```

- `AtRowWriter<S>::Add(const S&)` writes one info line for the row at once. The server writes no info line of its own for a rows form.
- A row is written with its Open and Close texts, its fields, and the separator rules below.
- A `RawString` field is written as it is, up to the first CR or LF.
- An absent trailing optional field is left out of the line, as for arguments.

---

## The Line on the Wire

| Form | Line |
|---|---|
| record | `+JOINED: "net",-40,"pw"`, with the name and a colon as the prefix |
| row | `+CWLAP: (4,"net",-72,"aa:bb",6)` |
| value with a reply prefix that ends in a colon | `AT version: 2.2.0.0(Jul 1 2022)` |
| value with a reply prefix that does not end in a colon | `+CIFSR:STAIP,"192.168.1.9"` |

After a reply prefix that ends in `:`, one blank is written and one blank is skipped by the client. After any other reply prefix, a comma is written and skipped by the client. A set with no reply prefix of the second kind has no code for the comma.

---

## Costs

- A set with no record and no row has no field, row or row-writer code. The server's and the client's code for a value reply is the same as before the rows.
- A row costs its field readers and writers: one instantiation for each field type of each row, and the row table in flash (Open, Close, and one range for each field).
- A client with `AtRows<N>` has N times the largest row size of RAM, and a rows reply uses no other request memory.
- Measured on the ESP-AT client (see [EspAt](Descriptions/EspAt.md#sizes)): the client with `AtRows<8>` and `AtEcho` is 1684 bytes of text larger than the plain client. Its object is 592 bytes larger, which is the 8-row storage and its two counts. The row type there is `EspAtAccessPoint`.
- Each `Rows` or `ReadRows` reply type costs its own text: the ESP-AT set has one row type, so it has one set of row readers and writers.
