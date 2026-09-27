# Helper

Helper adapters that extend base peripherals with higher-level functionality.

## I2C

| Helper | Description |
|--------|-------------|
| [I2CMutexAdapter](/docs/Periphery/Adapter/Helper/I2C/I2CMutexAdapter.h) | Wraps I2C operations with a user-provided lock/unlock callback |

## TIM

| Helper | Description |
|--------|-------------|
| [ITIMHelper](/docs/Periphery/Adapter/Helper/TIM/ITIMHelper.h) | Base helper for timer channel operations — frequency calculation, prescaler, compare |
| [TIMOutputCompareHelper](/docs/Periphery/Adapter/Helper/TIM/TIMOutputCompareHelper.h) | Extends ITIMHelper with output compare features — frequency/duty control, PWM |
