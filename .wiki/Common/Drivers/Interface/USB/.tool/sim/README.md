# USB device host tests (simulator)

Host tests for the USB device stack on the PC. A software controller (`SimUSBDeviceAdapter.h`) stands in for the
hardware and enforces the rules of the `USBDeviceAdapter` contract, and a scripted host drives the core through it.

## Run

From this folder, with a g++ that supports C++23 (tested with g++ 15.2):

    sh run.sh

Each `Test*.cpp` is built into its own executable with `-std=c++23 -Wall -Wextra -Werror` and run. The script exits
non-zero when a test fails to build or any check fails. It works in Git Bash on Windows and on Linux or macOS. The
`CXX` variable selects another compiler. Build output goes to a temporary directory outside the repository.

## Files

- `SimUSBDeviceAdapter.h`: the simulated controller. Injections (bus reset, SETUP, suspend, resume, errors) and host
  transactions (`HostIn`, `HostOut`) run the events they cause before they return.
- `shim/VHAL.h`: the PC stand-in for the VHAL umbrella header. It defines the `System` functions the tests reach. The tick is
  the port's unit: a 64-bit count of milliseconds (`msPerTick` 1), read from the wall clock here, since the shim has no SysTick.
  The port assumes the application calls `TickHandler()` once per millisecond; the shim's unit assumes the same. Each read also sets the low 32 bits that `GetTick32()` returns.
- `SimAssert.h`: `SIM_EXPECT` and `SIM_EXPECT_EQ`, with `SimTest::Finish()` for the exit code.
- `FixedList.h`: a fixed-capacity list for the records the tests keep. The callbacks run in interrupt context, so the
  tests never allocate; a list that is full sets its overflow flag.
- `Descriptors.h`: the descriptor bytes, written by hand (not from the builders), so a builder bug cannot hide in both.
- `SimHost.h`: builds SETUP packets and runs whole control requests the way a host does.
- `TestFunction.h`, `DeviceFixture.h`: a small bulk function that records what the core routes to it, and a device
  built on it.
- `TestAdapterRules.cpp`: the controller against the adapter contract, without the core.
- `TestEnumeration.cpp`: the Windows, Linux and macOS enumeration orders, strings, BOS and the MS OS 2.0 request.
- `TestControlStages.cpp`: a SETUP that interrupts an IN or OUT data stage, SET_ADDRESS abandoned, and the zero-length
  packet rule.
- `TestStallAndHalt.cpp`: unanswered requests stall, the next SETUP clears the stall, endpoint halts and their clear.
- `TestConfiguration.cpp`: SET_CONFIGURATION (0, repeated, unknown, a failing `OnConfigured`), GET_CONFIGURATION,
  GET_INTERFACE and SET_INTERFACE.
- `TestPowerState.cpp`: suspend, resume and remote wakeup, enabled and not enabled, and a bus reset clearing the flag.
- `TestSetFeatureRequest.cpp`: SET_FEATURE and CLEAR_FEATURE with the wrong shape, and the status stage of an accepted request:
  a zero-length IN on 0x80 with no OUT armed, the remote wakeup flag set once it has started, and the endpoint halt in place
  before the host takes it.
- `TestBenchModes.cpp`: the bench's IN modes (Source, SourceAndSink, Loopback) against a bus reset at each point of the commit
  of a completed stage: inside the claim after the host's read, before the commit, and after it. The completed stage is
  counted once and never sent again, and the new session starts at the first unsent byte (Source) or from an empty loopback
  ring (Loopback).
- `TestRouting.cpp`: class and vendor requests and bulk completions routed to the owning function.
- `TestVendorBulk.cpp`: the vendor bulk function's buffered FIFOs at their limits: a full OUT FIFO that NAKs the rest, an
  empty IN FIFO, a wrap-around, a transfer ending on a 64-byte boundary, and a zero-length packet.
- `SimFsdevController.h`: a register model of the fsdev block (EPnR write rules, buffer conflicts that mask the STAT
  reads, the packet memory and the buffer table, and the ISTR flags). The port reaches it through the register hooks of
  `SimFsdevFamily`, which a family may define; the STM32 families do not, and their accesses stay volatile. The controller
  has test hooks: `beforeEprWrite` runs before an EPnR write so a test can let a token complete in between, and
  `afterEprWrite` runs once the write is applied, which lets a token land in the time the port spends after a register
  access (the copy of a packet). `RaiseIstrFlags()` raises the ISTR flags the controller sets by itself (RESET).
- `TestFsdevPort.cpp`: `USBFsdev<SimFsdevFamily>` on that model, with a scripted host that checks each packet's PID and
  drops a wrong one while ACKing it. It covers double-buffered and single-buffered IN and OUT transfers, CLEAR_FEATURE(HALT)
  in the middle of a transfer (with a completed packet still pending), a token that arrives between the take-back and the
  re-arm of `ClearStall()` (NAKed by the buffer conflict), a single-buffered endpoint reopened double-buffered after an
  unserved completion, Stall() during a buffer conflict, and Stall() then ClearStall() on a single-buffered endpoint (Stall
  ends the armed transfer, so the next IN gets NAK). The double-buffered cases count NAKed tokens with the CPU serving each
  interrupt at once and a host token arriving in the copy window of each interrupt (the copy takes time after the first
  register write): on a double-buffered endpoint the port releases the prefilled buffer before the copy, so no packet in
  the copy window is NAKed. A case under interrupt latency of one further token checks delivery only, since no order of
  one release per completion avoids every NAK there. A double-buffered OUT packet that arrives after the transfer is held
  for the next StartOutTransfer(), and is dropped by a close. A completed OUT packet that the interrupt has not served yet
  is taken before StartOutTransfer() hands a buffer back, and before CLEAR_FEATURE(HALT) resets the toggles, so it is
  delivered with the buffer it was stored in. Zero-length packets, Stall, close and bus reset are checked with both
  buffers in use.

## Notes

- `SimHost::Control()` returns `ok` only when the request ended with its status stage (a zero-length completion on 0x00
  after an IN request with data, on 0x80 otherwise). A request the core stalls returns `accessError`. A request the core
  left unfinished returns `notReady`: a stage still armed after the bound, no status, or host data no OUT stage took.
- The controller reports what the contract does not list, for the tests only:
  - `GetDataToggle(address)`: the toggle of the next packet. The halt test and the zero-length packet tests use it to
    check that a clear resets the toggle and that a zero-length packet is sent.
  - `GetControlCompletionCount()` and `GetLastControlCompletion()`: the endpoint 0 completions the core received. `Control()`
    uses them to tell a finished request from one the core left half done.
  - `GetToggleMismatchCount()`: endpoint 0 stages that started with DATA0. Every stage starts at DATA1 (USB 2.0 section
    8.6.2), and a SETUP is DATA0. The count stays zero unless the start of a stage is broken, which a test checks.
  - `FinishResume()`: ends the resume signalling that `RemoteWakeup()` started, and raises `onResume` as the port does.
    `RemoteWakeup()` itself returns before `onResume` runs.
- `HostIn()` copies into a caller buffer and returns the byte count through an out parameter. A buffer too small for the
  transfer returns `invalidParameter` and leaves the transfer armed.
- The controller models the G431 fsdev limits: 8 endpoint numbers, 1 KB of packet memory (endpoint 0 takes 128 bytes of
  it, and the buffer table 8 bytes for each number up to the highest one set by `SetHighestEndpointNumber()`), and one halt
  shared by both endpoint 0 directions. An IN block is its packet size rounded up to an even size, and an OUT block is rounded as the port rounds it.
