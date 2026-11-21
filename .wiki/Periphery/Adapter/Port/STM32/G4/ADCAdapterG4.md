# ADCAdapterG4

The [ADC adapter](../../../ADCAdapter.md) on the STM32G4 ADC instances. The adapter is `ADCAdapter<ADC_TypeDef>` with no DMA handle type, so this port has no DMA path. `ReadByteArrayAsync()` runs on the end-of-conversion and overrun interrupts.

Header: `#include <Adapter/Port/STM32/G4/ADCAdapterG4.h>`

Alias: `AADC`. The constructor takes the ADC instance: `AADC adc1 = { ADC1 };`.

Enabled by `#define VHAL_STM32_G4_ADC`, which `Periphery/Adapter/Port/STM32/Adapter.h` checks before it includes the port.

## Refused

| Call | Result |
|------|--------|
| `SetInjectedChannel()` | `notSupported` |

The injected group is not implemented on this port. A DMA is not attached on this port, so there is no DMA check to run.
