# ModbusResult

The outcome of one Modbus request.

```cpp
struct ModbusResult {
	ResultStatus status;
	uint8 exception;
};
```

`status` is a `ResultStatus`. `exception` is set only for an exception reply, and it is 0 for every other status.

| Status | Meaning |
|---|---|
| `ok` | The reply matched the request |
| `error` | An exception reply. `exception` holds the code the slave sent: 01, 02, 03 or 04 |
| `timeout` | No reply within the timeout, after all attempts |
| `crcError` | The last reply had a bad CRC |
| `invalidData` | The last reply broke a check: a short or long reply, a wrong address or function code, a wrong echo, or a frame that broke on the line |
| `invalidParameter` | A quantity of 0 or over the limit. Nothing was sent |
| `readError` | The transport failed to receive |
| `writeError` | The transport failed to send |

Exceptions are returned as the slave sent them, not as the client's own error. Only `ok` and `error` come from a valid reply.
