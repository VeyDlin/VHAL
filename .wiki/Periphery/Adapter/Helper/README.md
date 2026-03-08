# Helper

Helper adapters that extend base peripherals with higher-level functionality.

## I2C

| Helper | Description |
|--------|-------------|
| [I2CMutexAdapter](/docs/Periphery/Adapter/Helper/I2C/I2CMutexAdapter) | Wraps any I2C adapter with mutex lock/unlock around every operation |

## TIM

| Helper | Description |
|--------|-------------|
| [ITIMHelper](/docs/Periphery/Adapter/Helper/TIM/ITIMHelper) | Base helper for timer channel operations — frequency calculation, prescaler, compare |
| [TIMOutputCompareHelper](/docs/Periphery/Adapter/Helper/TIM/TIMOutputCompareHelper) | Extends ITIMHelper with output compare features — frequency/duty control, PWM |
