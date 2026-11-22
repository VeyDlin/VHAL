# AtEcho

`Common/Utilities/AT/Modules/AtEcho.h`: the echo module of the client. A module that echoes each request back (ATE1) would make the client read its own request as a reply line. `AtEcho` drops that line.

---

## Table of Contents

- [Declaration](#declaration)
- [What It Drops](#what-it-drops)
- [Without It](#without-it)
- [Echo Is Not Assumed](#echo-is-not-assumed)
- [Costs](#costs)
- [Tests](#tests)

---

## Declaration

```cpp
#include <Utilities/AT/Modules/AtEcho.h>

AtClient<Sensor, ClientBinding, AtQueue<2>, AtEcho> sensor(out);
```

`AtEcho` is a tag type that the client's module list holds. It has no members and no calls.

---

## What It Drops

The client sends a request, and the echo of that request is the first line that comes back. With `AtEcho` listed, that line is dropped when all of these hold:

- the line is the first line after the request was sent;
- the request is the head of the queue and is open;
- the line has no overflow, so it fits the client's line buffer;
- the line is exactly the text that was sent, such as `AT+TEMP?`.

Only the first line after a send is compared. The echo of a later request is compared when that request is sent. The compare runs before the URC match, so a line that equals the request text is dropped even when it starts like a URC.

---

## Without It

Without `AtEcho` in the modules, the echoed line is an unknown line. The client ignores it, and the reply goes on. The result is the same, unless the echo matches a final line such as `OK`, which a request text never does.

So a client without the module works with echo on and with echo off. The module is a convenience for the case where the echo is on, and it avoids any reading of the request text as a reply.

---

## Echo Is Not Assumed

The client does not send `ATE0` or `ATE1` by itself. The application chooses:

```cpp
void OnEchoOff(AtStatus status) {
	// status is ok when the module answered OK
}

sensor.Value<"E", &OnEchoOff>(uint8{0});   // ATE0, which needs .Echo() in the description
```

The ESP-AT reference says the stock firmware echoes, so an ESP-AT client lists `AtEcho`. A module with echo off needs no echo handling, and the client works without `AtEcho`.

---

## Costs

- The compare is compiled in only when the client lists `AtEcho`. Without the module there is no compare, and no flag is set by a send.
- The ESP-AT client's text is 11086 bytes with `AtEcho` and `AtRows<8>`, and 9402 bytes with neither module (see [EspAt](Descriptions/EspAt.md#sizes)). That difference covers the echo and the rows together, so the echo's share is not measured alone.

---

## Tests

- `TestAtClient.cpp`: the echo of a request is dropped, and an echo that is not the request text is an unknown line.
- `TestAtTranscript.cpp`: the transcripts with `AtEcho` in the client, such as `echo_and_urc_arguments.txt`, and the chained request of `TestChainFinalIsOneStatus`.
- `TestAtEspAt.cpp` and the `espat_*.txt` transcripts: every transcript is an ESP-AT session with the echo on, fed whole, cut at every byte, and one byte at a time.
