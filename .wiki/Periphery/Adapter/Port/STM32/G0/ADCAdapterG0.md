# ADCAdapterG0

The [ADC adapter](../../../ADCAdapter.md) on the STM32G0 ADC1. The adapter is `ADCAdapter<ADC_TypeDef, ADMA>`, so the DMA is the `DMAAdapterG0` attached with `SetDMA()`.

Header: `#include <Adapter/Port/STM32/G0/ADCAdapterG0.h>`

Alias: `AADC`. The constructor takes the ADC instance: `AADC adc1 = { ADC1 };`.

Enabled by `#define VHAL_STM32_G0_ADC`, which `Periphery/Adapter/Port/STM32/Adapter.h` checks before it includes the port.

## Asynchronous Read by DMA

`ReadByteArrayAsync(uint8* buffer, uint16 size)` with a DMA attached moves the conversions by DMA. The buffer holds one item per sample:

- 6 and 8-bit resolution: one byte per sample.
- 10 and 12-bit resolution: one halfword per sample.

The count given to the DMA is `size` bytes, rounded down to whole samples. The DMA divides it by its own item width, which the check below has fixed to the resolution, so a 6 or 8-bit conversion fills one byte per sample and never overruns the buffer.

Before it starts the DMA, the adapter checks it with `CheckPeripheral()`:

| Check | Required value |
|-------|----------------|
| Request | `Adc1` |
| Direction | `PeripheralToMemory` |
| Peripheral width | One item per sample: 8 bits at 6 and 8-bit resolution, 16 bits at 10 and 12-bit resolution |
| Memory width | The same width as the peripheral |

On a mismatch the read returns `invalidParameter` and the ADC state goes back to `ready`.

6 and 8-bit resolution with left alignment is refused with `invalidParameter`: a byte read of the left-aligned register takes the wrong bits.

The adapter does not set the DMAMUX request or the widths. The application configures the DMA with `SetParameters()`.

## Refused

| Call | Result |
|------|--------|
| `ReadByteArrayAsync()` with a DMA whose request, direction or width does not match | `invalidParameter` |
| 6 or 8-bit resolution with left alignment and a DMA attached | `invalidParameter` |
| `ReadByteArrayAsync()` while a read runs | `busy` |
| `InjectedInitialization()`, `SetInjectedChannel()` | `notSupported` |

## Completion and Errors

The DMA completion calls `onInterrupt(Irq::Conversion)`. A DMA error calls `onError` with `Error::Overrun`. The overrun interrupt is enabled in DMA mode too, so an overrun of the conversions calls `onError` with `Error::Overrun` as well.
