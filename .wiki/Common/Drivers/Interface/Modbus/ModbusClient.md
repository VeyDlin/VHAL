# ModbusClient

A Modbus client over a transport and a framer. Each request is one transaction: it sends the frame, waits for one reply, and checks
the reply. The client is a template on the transport ([RtuTransport](RTU/RtuTransport) on RTU), the framer (`RtuFramer`), and a
captureless lambda that returns the settings.

## Settings

```cpp
using Plc = ModbusClient<Link, RtuFramer, +[] {
	return ClientBuilder()
		.SlaveAddress(1)
		.Timeout(500)
		.Retries(2);
}>;
```

| Setting | Default | Rule (compile error when broken) |
|---|---|---|
| `SlaveAddress(n)` | 1 | 1 to 247. Address 0 is the broadcast address: use `Broadcast()` |
| `Timeout(ms)` | 500 | 1 to 4294967 ms. It is the wait per attempt for the reply, counted from the end of the send and its turnaround. A send also waits at most this long for a quiet line |
| `Retries(n)` | 2 | Any count. The client makes `n + 1` attempts at most |
| `Lock()` | off | Adds the lock code: see the lock section |

## Reads and writes

```cpp
Plc plc(link);

std::array<uint16, 4> values{};
ModbusResult read = plc.ReadHoldingRegisters(0x0000, values);     // quantity from the array size
if (read.status == ResultStatus::error) {
	// read.exception holds the exception code the slave sent (02 for an address outside its view)
}

plc.WriteSingleRegister(0x0010, 1234);
std::array<uint16, 2> pair = {1, 2};
plc.WriteMultipleRegisters(0x0020, pair);

std::array<bool, 8> coils{};
plc.ReadCoils(0x0000, coils);
plc.WriteSingleCoil(0x0003, true);
```

| Function | Code | Values |
|---|---|---|
| `ReadCoils` | 01 | `std::array<bool, N>&` or `std::span<bool>` |
| `ReadDiscreteInputs` | 02 | `std::array<bool, N>&` or `std::span<bool>` |
| `ReadHoldingRegisters` | 03 | `std::array<uint16, N>&` or `std::span<uint16>` |
| `ReadInputRegisters` | 04 | `std::array<uint16, N>&` or `std::span<uint16>` |
| `WriteSingleCoil` | 05 | `bool` |
| `WriteSingleRegister` | 06 | `uint16` |
| `WriteMultipleCoils` | 0F | `std::span<const bool>`, 1 to 1968 coils |
| `WriteMultipleRegisters` | 10 | `std::span<const uint16>`, 1 to 123 registers |

With an array the quantity is checked at compile time: `FC 03 reads at most 125 registers` is an error for a larger array. With a span
the quantity is checked when the call runs, and a quantity of 0 or over the limit returns `invalidParameter` with nothing sent.

## Results

Every function returns a [`ModbusResult`](ModbusResult). A reply that matched the request gives `ok`. An exception reply gives `error`
with the code in `exception`. The other statuses are listed on the [ModbusResult](ModbusResult) page.

## Retries

- A timeout, a CRC error, or a reply that breaks a check (`invalidData`) is sent again, up to the retry count.
- An exception reply, `invalidParameter`, and a transport failure (`readError` or `writeError`) are not sent again. A `writeError` also
  covers a write that the adapter reports as timed out after its bytes left the line, so the request is not sent a second time.
- Each attempt clears the receive ring before it sends. Modbus RTU has no transaction id, so a reply to an earlier attempt that
  arrives after that clear is taken as the answer to the retry. Such a reply can be one timeout old.
- A timed-out write may have reached the device, so a retry applies it again. For a pulse or a counter, use `Retries(0)`.

## Lock

A client is not reentrant. Two tasks share one client through a lock: add `Lock()` to the builder, name a lock type that has `Lock()`
and `Unlock()`, and give the client a lock object.

```cpp
struct MutexLock {
	void Lock();
	void Unlock();
};

using SharedPlc = ModbusClient<Link, RtuFramer, +[] {
	return ClientBuilder()
		.SlaveAddress(1)
		.Lock();
}, MutexLock>;

MutexLock plcLock;
SharedPlc plc(link, &plcLock);
```

Without `Lock()` the client has no lock code. A `Lock()` without a lock type is a compile error. A locked client built without a lock
object returns `noInit` for each request and sends nothing.

## Checks on the reply

In order: the framer checks the CRC (`crcError`), then the slave address (`invalidData`); then the function code and the exception
form (`invalidData` for a bad exception length); then the length (the byte count of a read, 8 bytes for a write); then a write echo
must have the same address and value or quantity (`invalidData`).

## Broadcast

`plc.Broadcast()` gives the write functions to address 0. See [ModbusBroadcast](ModbusBroadcast).

## Sizes

The numbers are the output of `Common/Drivers/Interface/Modbus/.test/size/size.cmake`, row `ClientHolding` (the `.cpp` in that folder). The
script builds it with `arm-none-eabi-g++` 15.2 for a Cortex-M4, `-std=gnu++23 -mcpu=cortex-m4 -mthumb -fno-rtti -fno-exceptions -Os`.
The probe transport is declared and not defined, so the UART and the timing are not in these numbers. The text includes the 512-byte
CRC table once.

| Probe | text | data | bss |
|---|---|---|---|
| Client that reads holding registers only (`ReadHoldingRegisters`, array form) | 1082 B | 4 B | 520 B |

The bss is the two 256-byte frame buffers and the client object. Each function the client does not call costs nothing.
