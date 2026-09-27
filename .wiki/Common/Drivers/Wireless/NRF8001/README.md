# NRF8001 BLE driver

The NRF8001 transport is exposed through `BLEPeripheral`. It receives an SPI adapter and request, ready, and reset GPIO adapters. Add services and characteristics before calling `begin()`, then call `poll()` regularly to process device events:

```cpp
BLEPeripheral peripheral(&spi, &requestPin, &readyPin, &resetPin);
BLEService batteryService("180F");
BLEUnsignedCharCharacteristic batteryLevel("2A19", BLERead | BLENotify);

peripheral.addAttribute(batteryService);
peripheral.addAttribute(batteryLevel);
peripheral.setLocalName("Battery sensor");
peripheral.begin();

void Loop() {
    peripheral.poll();
}
```

Keep the service and characteristic objects alive for as long as the peripheral uses them. `BLECharacteristic::setValue` and the typed characteristic setters return `bool`; they do not return `Result<T>`. `BLEPeripheral::begin()` and `poll()` return `void`.

The SPI adapter's `WriteReadArray` returns `ResultStatus`. The NRF8001 transport uses the received byte buffer for the ACI transfer, including when the adapter reports a non-OK status; the platform SPI implementation must leave the received byte available in that buffer. The transport does not expose that per-byte SPI status as a BLE `Result`.
