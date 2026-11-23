# Engines

An engine computes what a transform describes. A transform says which algorithm (a CRC) and the engine says how.

An engine is a type with no state. It has:

- `synchronous`, `true`: the call returns the result.
- `Supports<Algorithm>()`: whether the engine computes the algorithm. The builder refuses a transform whose engine says no.
- `Run<Algorithm>(data)`: the result of the algorithm over the bytes.

`VubEngine<Engine>` checks the first; `VubEngineFor<Engine, Algorithm>` checks all three. The algorithm is a type with its parameters
in `Algorithm::parameters`; for a CRC it is `VubCrcAlgorithm<parameters>`.

The CRC engines:

- [VubCrcTable](VubCrcTable): a 256-entry table in flash, one lookup per byte. The default.
- [VubCrcBitwise](VubCrcBitwise): no table, one bit at a time.

The shared CRC code is in [VubCrcCore](VubCrcCore).
