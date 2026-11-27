# AtAwait

`Common/Utilities/AT/Modules/AtAwait.h`: the await module of the client. A client that lists it has the `Await` calls. Each one is a plain call that returns its reply, so a task can write `Result<int16, AtStatus> celsius = sensor.AwaitRead<"+TEMP">();` and wait for the answer. The client itself never waits: the wait is in the task that calls `Await`, and the requests are still answered in `Process()`.

---

## Table of Contents

- [Declaration](#declaration)
- [The Calls](#the-calls)
- [The Thread Rule](#the-thread-rule)
- [The Wait and the Safety Limit](#the-wait-and-the-safety-limit)
- [The Copy](#the-copy)
- [Abandoned Requests](#abandoned-requests)
- [Rules Checked at Compile Time](#rules-checked-at-compile-time)
- [Costs](#costs)
- [Tests](#tests)

---

## Declaration

```cpp
using Sensor = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Defaults(AtDefaults().TimeoutMs(2000))
		.Command<"+TEMP">().Read<int16>("celsius")
		.Command<"+JOIN">().Write(Argument<String<32>>("ssid")).TimeoutMs(15000)
		.Command<"+LIST">().Rows(NetworkRow);
}>;

using Binding = AtClientBinding<>;

// AtAwait is one of the client's modules. AtRows<8> is needed for AwaitRows and AwaitReadRows.
AtClient<Sensor, Binding, AtQueue<2>, AtRows<8>, AtAwait> sensor(out);
```

The application sets the RTOS handle once, as for `WaitCondition`:

```cpp
System::rtosDelayMsHandle = [](uint32 delay) -> bool {
	return RtosDelay(delay);      // the port's delay, which gives the processor to the other tasks
};
```

---

## The Calls

Each call takes the same arguments as its plain call, and returns the reply:

| Call | Plain call | Returns |
|---|---|---|
| `AwaitRead<"+X">()` | `Read<"+X", &OnValue>()` | `Result<T, AtStatus>`, a string as `String<N>`, a record as its struct |
| `AwaitRun<"+X">()` | `Run<"+X", &OnStatus>()` | `AtStatus` for a run form with no reply, `Result<T, AtStatus>` with one |
| `AwaitWrite<"+X">(values...)` | `Write<"+X", &OnStatus>(values...)` | `AtStatus` |
| `AwaitValue<"X">(values...)` | `Value<"X", &OnStatus>(values...)` | `AtStatus`, for a basic command or a builtin such as `E` |
| `AwaitReadRows<"+X">()` | `ReadRows<"+X", &OnRows>()` | `Result<AtRowCopy<Row, N>, AtStatus>` |
| `AwaitRows<"+X">()` | `Rows<"+X", &OnRows>()` | `Result<AtRowCopy<Row, N>, AtStatus>` |
| `AwaitSendData<"+X">(values..., data)` | `SendData<"+X", &OnDone>(values..., data)` | `AtStatus` |
| `AwaitPing()` | `Ping<&OnPing>()` | `AtStatus`, the status of the bare `AT` |

`AwaitRead<"+TEMP">()` queues `AT+TEMP?`, waits, and returns the value. A `+CME ERROR: 3` reply returns `Result` with the error `AtStatus{error, 3}`. A full queue returns `busy` at once, without waiting.

`AwaitRows` and `AwaitReadRows` return rows through `AtRowCopy`, which is described in [The Copy](#the-copy).

Each call once, in a task (never in `Process()`), with the line it sends:

```cpp
Result<int16, AtStatus> temp = sensor.AwaitRead<"+TEMP">();                       // AT+TEMP?
AtStatus led = sensor.AwaitWrite<"+LED">(true);                                   // AT+LED=1
AtStatus quit = sensor.AwaitRun<"+CWQAP">();                                      // AT+CWQAP, a run form with no reply
Result<String<15>, AtStatus> ip = sensor.AwaitRun<"+CIFSR">();                    // AT+CIFSR, a run form with a value
AtStatus echo = sensor.AwaitValue<"E">(uint8{0});                                 // ATE0
Result<AtRowCopy<Network, 8>, AtStatus> list = sensor.AwaitRows<"+LIST">();       // AT+LIST, the rows, copied
Result<AtRowCopy<Network, 8>, AtStatus> scan = sensor.AwaitReadRows<"+SCAN">();   // AT+SCAN?, the rows, copied
AtStatus sent = sensor.AwaitSendData<"+CIPSEND">(uint16{5}, std::span<const uint8>(kPayload));   // AT+CIPSEND=5, then the data
AtStatus alive = sensor.AwaitPing();                                              // AT
```

`AwaitRun` returns an `AtStatus` for a run form with no reply and a `Result` for one with a value. The Await calls need the same modules as their plain calls: `AwaitRows` and `AwaitReadRows` need `AtRows`, and `AwaitSendData` needs `AtDataMode` in the set.

---

## The Thread Rule

**Never call an `Await` call in the `Process()` context.** That means a handler of the client, or any code that `Process()` calls. The `Await` call waits for `Process()` to finish the request, and `Process()` cannot finish it while it is waiting for itself. The wait then ends at the safety limit with `timeout`, and the entry is abandoned.

Call `Await` in an RTOS task. Run `Process()` in the main loop or in another task.

```cpp
void SensorTask() {
	Result<int16, AtStatus> celsius = sensor.AwaitRead<"+TEMP">();

	if (celsius.IsOk()) {
		// use celsius.Value()
	}
}

void MainLoop() {
	sensor.Process(System::GetTick32());     // another task, never inside SensorTask
}
```

---

## The Wait and the Safety Limit

- The wait is `WaitCondition::WaitFor<RtosYield>` on the request's done flag. `RtosYield` gives the processor to the other tasks for one millisecond between polls, through `System::rtosDelayMsHandle`.
- The clock is `System::GetTick32()`, the same clock that the application passes to `Process()`. The request's timeouts and the wait's limit are both in that clock's milliseconds.
- The limit is the queue depth times the set's longest timeout plus the drain time (the set's default timeout):

  ```
  limit = depth * (longest timeout + drain time)
  ```

  For the declaration above, the depth is 2, the longest timeout is 15000 ms and the drain is 2000 ms, so the limit is 34000 ms. A request that waits behind another can take the time of both, so the limit covers a full queue.

- When `Process()` finishes the request first, the wait returns at once, with the outcome of the request. A request that times out in `Process()` ends with `timeout` at its own timeout plus the drain: for a 2000 ms command, 4000 ms.
- When the limit passes first, `Await` returns `timeout`, and the entry is abandoned (see [Abandoned Requests](#abandoned-requests)).

---

## The Copy

The outcome is copied out of the request's queue entry before the entry is freed:

- A value, a string or a record is copied into the `Result`. A string is a `String<N>`, a copy, because a view would end when the entry is freed.
- A rows reply is copied into an `AtRowCopy<Row, N>`, which holds up to N rows and owns them. It stays valid after the call, and after later requests. Rows past N are counted in `Skipped()`, and the status stays `ok`, as for `Rows`.
- A plain call with no reply returns its `AtStatus` only.

The entry is freed by the waiting task, once the copy is made. Process does not touch a done entry, so the copy is safe without a critical section.

`AtRowCopy` is a value in the caller's frame: its size is N times the size of the row, plus two counts. A large N on a small stack should be sized with care.

---

## Abandoned Requests

If the wait gives up, the entry is abandoned, and no task waits for it any more:

- If the request is still queued, `Process()` drops it at the head of the queue without sending it.
- If the request is on the wire, `Process()` finishes it as usual: it times out, drains, and then the entry is freed. Its outcome is discarded, and no handler is called.

The queue keeps working meanwhile. A request behind the abandoned one is sent in its turn.

A done entry is never abandoned. When the wait ends at the limit, the entry is read again in a critical section: if `Process()` has finished it in the meantime, the wait takes its outcome instead of abandoning it.

---

## Rules Checked at Compile Time

| Rule | Message |
|---|---|
| an `Await` call without `AtAwait` in the client's modules | `AwaitRead needs AtAwait among the client modules` (the name of the call) |
| `AtAwait` listed twice | `AtAwait is listed twice in the client modules` |
| `AwaitRows` or `AwaitReadRows` without `AtRows` | `AwaitRows needs the AtRows module in the client modules` |
| `AwaitSendData` without `AtDataMode` in the set | `AwaitSendData needs AtDataMode among the modules of the set` |
| `AwaitWrite` on a prompt command | `a prompt command takes AwaitSendData, not AwaitWrite` |
| `AwaitRead` on a `ReadRows` command | `AT command's read form is ReadRows: call AwaitReadRows` |

The snippets are in `Common/Utilities/AT/.test/BuilderErrors/await_*.cpp`.

---

## Costs

- A client without `AtAwait` has no `Await` code, and no reference to `WaitCondition` or to the RTOS handle. Its size is unchanged.
- With `AtAwait`, each queued request has an abandonment flag and two row counts, about 12 bytes on a 32-bit MCU.
- With `AtAwait` and `AtRows<N>`, every queue entry holds the whole row storage, N rows of the set's largest row type, since an awaited rows reply is copied into its entry. A client with `AtQueue<1>` pays for one entry only.
- The `Await` calls add code for each form they are used with. A form that is not called costs nothing.
- Measured on the ESP-AT client (see [EspAt](../Descriptions/EspAt.md#sizes)): `AtAwait` adds 1742 bytes of text to the client with `AtEcho` and `AtRows<8>`, which is 12979 bytes in all. The client object is 2792 bytes, since `AtQueue<2>` holds the row storage in each of its two entries.

---

## Tests

`Common/Utilities/AT/.test/TestAtAwait.cpp` runs the client on the host with a scripted module. The RTOS handle advances the fake tick by one millisecond, runs `Process()` with that tick, and answers each request line that the client writes. The tests check:

- each form returns its value or its status, and a `+CME ERROR` reply returns its code;
- a request queued behind another is answered after it, in one wait;
- a full queue returns `busy` at once, with no wait and no bytes written;
- a module that never answers ends the wait with `timeout` after 201 ms (100 ms of the request and 100 ms of the drain), across the 32-bit wrap;
- with no `Process()` running, the wait ends at the limit (800 ms for a queue of two and a longest timeout of 300 ms), the entry is dropped without being sent, and the next request works;
- a request that was sent, and then abandoned, is drained by `Process()` and freed with no handler call;
- the rows are copied, five rows give four stored and one skipped, and the copy is valid after a later request;
- `AwaitPing` returns `ok` for `OK`, `error` for `ERROR`, `error cme 3` for `+CME ERROR: 3`, and `busy` on a full queue with no bytes written. A module that never answers gives `timeout` after 201 ms, and the bytes are `AT` then the drain's `AT`.

`TestAtEspAt.cpp` compiles every Await call of the ESP-AT client (`EspAtAwaitForms`), with `AtAwait`, `AtRows<8>` and `AtDataMode` in the set. It does not run them. The size objects `SizeEspAtAwait.cpp` make the same calls, for the flash figure above.
