# EventMode

Enum that controls how `Event::Wait()` interprets the bit mask.

## Values

| Value | Description |
|-------|-------------|
| `EventMode::waitAnyBits` | Return when **any** of the masked bits are set (default). Useful when multiple independent events can wake the thread. |
| `EventMode::waitAllBits` | Return only when **all** masked bits are set simultaneously. Useful for synchronization barriers — wait until all producers have reported. |

## Usage

```cpp
OS::Event event(500ms, 0b11);

// Wait for ANY bit (either producer can wake us)
tEventBits bits = event.Wait(OS::EventMode::waitAnyBits, 0b11);

// Wait for ALL bits (both producers must signal before we proceed)
tEventBits bits = event.Wait(OS::EventMode::waitAllBits, 0b11);
```

See [Event](Event.h) for full documentation.
