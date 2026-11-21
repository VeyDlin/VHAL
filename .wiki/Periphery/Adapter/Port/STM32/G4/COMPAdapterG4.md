# COMPAdapterG4

The [COMP adapter](../../../COMPAdapter.md) on the STM32G4 comparators. COMP1 to COMP3 exist on the STM32G411. COMP1 to COMP4 exist on the STM32G414, G473, G474, G483 and G484. COMP5 to COMP7 exist on the same five chips.

Header: `#include <Adapter/Port/STM32/G4/COMPAdapterG4.h>`

Class: `COMPAdapterG4`. Alias: `ACOMP`. The constructor takes the comparator: `ACOMP comp1 = { COMP1 };`.

Enabled by `#define VHAL_STM32_G4_COMP`, which `Periphery/Adapter/Port/STM32/Adapter.h` checks before it includes the port.

## SYSCFG Clock

The comparator registers sit in the SYSCFG block, and writes are dropped while its clock is off. The BSP enables the SYSCFG clock (`LL_APB2_GRP1_PERIPH_SYSCFG`) before the first `Initialization()`. While the clock is off, `Initialization()` and `Enable()` return `notReady`. The clock is checked before the `beforePeripheryInit` hook runs.

## Interrupts

The BSP routes the comparator vectors to `IrqHandler()` of every comparator adapter that shares a vector:

| Chips | Vectors |
|-------|---------|
| G431, G441, G471, G491, G4A1 | `COMP1_2_3_IRQn` and `COMP4_IRQn` |
| G411 | `COMP1_2_3_IRQn` |
| G473, G474, G483, G484, G414 | `COMP1_2_3_IRQn`, `COMP4_5_6_IRQn` and `COMP7_IRQn` |

Each adapter serves only its own EXTI line and clears only that flag, so a shared vector is safe. The adapter does not touch the NVIC.

## Refused

| Call | Result |
|------|--------|
| `Initialization()` or `Enable()` with the SYSCFG clock off | `notReady` |
