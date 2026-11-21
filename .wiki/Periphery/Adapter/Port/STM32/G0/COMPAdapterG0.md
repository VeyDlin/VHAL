# COMPAdapterG0

The [COMP adapter](../../../COMPAdapter.md) on the STM32G0 comparators. COMP1 and COMP2 exist on the STM32G051, G061, G071, G081, G0B1 and G0C1. COMP3 exists on the STM32G0B1 and G0C1.

Header: `#include <Adapter/Port/STM32/G0/COMPAdapterG0.h>`

Class: `COMPAdapterG0`. Alias: `ACOMP`. The constructor takes the comparator: `ACOMP comp1 = { COMP1 };`.

Enabled by `#define VHAL_STM32_G0_COMP`, which `Periphery/Adapter/Port/STM32/Adapter.h` checks before it includes the port.

## Chips Without a Comparator

The whole class is guarded by the CMSIS define `COMP1`. The STM32G030, G031, G041, G050, G070 and G0B0 have no comparator, and the header compiles to nothing for them. The class and the alias `ACOMP` do not exist on those chips.

## SYSCFG Clock

The comparator registers sit in the SYSCFG block. The BSP enables the SYSCFG clock (`LL_APB2_GRP1_PERIPH_SYSCFG`) before the first `Initialization()`. While the clock is off, `Initialization()` and `Enable()` return `notReady`. The clock is checked before the `beforePeripheryInit` hook runs.

## Interrupts

The BSP forwards `ADC1_COMP_IRQHandler` to `IrqHandler()` of every comparator adapter. Each adapter serves only its own EXTI line (17, 18 or 20), so the shared vector is safe. The adapter does not touch the NVIC.

## Refused

| Call | Result |
|------|--------|
| `Initialization()` or `Enable()` with the SYSCFG clock off | `notReady` |
