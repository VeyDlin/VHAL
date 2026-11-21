# Audio Class

A USB Audio 1.0 function: an Audio Control interface, a microphone (device to host) and a speaker (host to device). The microphone is an isochronous IN endpoint and the speaker an isochronous OUT endpoint, one packet per 1 ms frame. Each path has an input terminal, a feature unit with master-channel mute and volume, and an output terminal. Sampling rates are 16, 32 and 48 kHz, 16-bit PCM, mono or stereo.

Headers:

- `#include <Drivers/Interface/USB/Class/Audio/USBAudioBuilder.h>`: the class builder, for the configuration builder
- `#include <Drivers/Interface/USB/Class/Audio/AudioFunction.h>`: the function, the rings and the events
- `#include <Drivers/Interface/USB/Class/Audio/AudioLayout.h>`: the parser the function reads its formats with (the application does not need it)

The function is built on the isochronous endpoints of the adapter (see [USBDeviceAdapter](../../../../../../Periphery/Adapter/USBDeviceAdapter.md#isochronous-endpoints)). The STM32G4 port's packet memory and its limits are on the [USBDeviceAdapterG4](../../../../../../Periphery/Adapter/Port/STM32/G4/USBDeviceAdapterG4.md) page.

## Class Builder

`USBAudioBuilder` plugs into `USBConfigurationBuilder::Function()` (see [Descriptor](../../Descriptor/README.md)). It writes the Audio Control interface, one AudioStreaming interface per path, the class-specific descriptors and the endpoints. The layout it gives the function is the contract: the function reads its formats, its endpoint addresses and its feature unit controls from the descriptors the builder wrote.

```cpp
using AppUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		// ... identities ...
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBAudioBuilder()
				.Name("Audio")
				.Microphone(1)
				.MicrophoneAddress(0x81)
				.MicrophoneRate(16000)
				.MicrophoneRate(48000)
				.MicrophoneVolume()
				.MicrophoneMute()
				.Speaker(2)
				.SpeakerAddress(0x02)
				.SpeakerVolume()
				.SpeakerMute()));
}>;

AudioFunction<192, 128> audio(AppUsb::Function(0));
```

| Method | Description |
|--------|-------------|
| `Microphone(channels)` | The microphone path, 1 or 2 channels. Given once. Any other value is a compile error |
| `MicrophoneRate(hz)` | A sampling rate of the microphone: 16000, 32000 or 48000, each once. Without any the microphone runs at 48 kHz. The rate at start is the first one listed |
| `MicrophoneVolume()`, `MicrophoneMute()` | The master-channel volume (-60 dB to 0 dB) and mute of the microphone's feature unit. Without them the unit has no controls |
| `MicrophoneAddress(address)` | The isochronous IN endpoint. Without it the configuration builder assigns the number |
| `MicrophoneName(text)` | The microphone's AudioStreaming interface string. Optional |
| `Speaker(channels)`, `SpeakerRate(hz)`, `SpeakerVolume()`, `SpeakerMute()`, `SpeakerAddress(address)`, `SpeakerName(text)` | The same for the speaker path, with an OUT endpoint |
| `Name(text)` | The Audio Control interface string. Optional |

The builder rejects, at compile time: no microphone and no speaker, a channel count outside 1 and 2, a rate other than 16000, 32000 or 48000, a rate listed twice (three rates are the most there are, so a fourth call is always a repeat), a rate given before its path, and an address of the wrong direction.

Each control names its path (`MicrophoneVolume()`, `SpeakerMute()`), so the chain says which feature unit a control belongs to.

The interface numbers are the Audio Control interface 0, the microphone's AudioStreaming interface 1 when it exists, and the speaker's next. The endpoints are the microphone's first, then the speaker's. The terminals and units have fixed IDs (microphone: input 1, feature unit 2, output 3; speaker: input 4, feature unit 5, output 6), so the function and a host tool read the same numbers. A path that is not given leaves its IDs unused, which the specification allows.

The interface association over all the function's interfaces has class 0x01, subclass 0x00 and protocol 0x00. The device class is 0xEF/0x02/0x01 because of it, as for every composite configuration.

## Descriptors

For a mono microphone and a stereo speaker, both with volume and mute at 48 kHz, the configuration holds these pieces (Audio Device Class 1.0 and Audio Data Formats 1.0):

| Piece | Bytes | Content |
|-------|-------|---------|
| Interface association | 8 | Interfaces 0 to 2, class 0x01 |
| Audio Control interface 0 | 9 | Class 0x01, subclass 0x01, protocol 0, no endpoints |
| Audio Control header | 8 + n | bcdADC 0x0100, wTotalLength over the header and the units (71 for this device), and the n AudioStreaming interfaces |
| Microphone input terminal | 12 | The microphone, type 0x0201, with the path's channels |
| Microphone feature unit | 8 + channels | The master channel's controls: bit 0 mute, bit 1 volume |
| Microphone output terminal | 9 | USB streaming type 0x0101, the source is the feature unit |
| Speaker input terminal | 12 | USB streaming type 0x0101, 2 channels, channel config 0x0003 |
| Speaker feature unit | 8 + channels | As the microphone's |
| Speaker output terminal | 9 | Speaker type 0x0301 |
| AudioStreaming 1, alternate 0 | 9 | No endpoints |
| AudioStreaming 1, alternate 1 | 9 | One endpoint |
| AS_GENERAL | 7 | The terminal link (the USB streaming output), delay 0, PCM |
| FORMAT_TYPE I | 8 + 3 × rates | 16-bit PCM in 2-byte subframes, the discrete sampling frequencies, 3 bytes each |
| Isochronous endpoint | 9 | IN for the microphone, synchronous (bmAttributes 0x0D); OUT for the speaker, adaptive (0x09). bInterval 1 |
| Class-specific endpoint | 7 | Bit 0 of bmAttributes is the sampling frequency control, set only when the stream lists more than one rate |
| AudioStreaming 2, alternate 0 and 1, and its pieces | | As the microphone's, with the speaker's terminal link |

The packet size of a path is one millisecond of its highest rate, plus one frame, times the channels times two bytes:

| Path | Highest rate | Frames | Packet |
|------|--------------|--------|--------|
| Mono microphone or speaker | 48 kHz | 49 | 98 bytes |
| Stereo speaker | 48 kHz | 49 | 196 bytes |

The extra frame lets an adaptive sink take a host that sends one more frame than the rate says. The microphone sends rate / 1000 frames of samples per packet, which is 16, 32 or 48 frames per channel, so its packet is shorter than the endpoint's size.

The class-specific endpoint is always written, so every isochronous audio endpoint has one. Bit 0 says whether the host may change the rate. Whether a stream with one rate needs it was not checked against the specification text, so that case is the one open point of the descriptors.

The descriptor bytes are checked byte for byte in `.tool/sim/TestAudioDescriptors.cpp`, with the expected arrays written by hand.

## Function

`AudioFunction<MicrophoneSamples, SpeakerSamples>` takes the layout of the function. The two template arguments are the ring sizes in samples: 16-bit values, both channels counted, so 192 is four mono frames at 48 kHz. A ring should hold at least one packet of its stream (96 samples for 48 kHz stereo). A smaller one works, but it pads or drops more. The function takes its formats from the descriptors, so the rates, channels and endpoint sizes come from the builder's chain and nowhere else.

A ring holds whole frames: `MicrophoneSamples` must be a multiple of the microphone's channels, and `SpeakerSamples` of the speaker's. A layout that does not match is refused at configuration: `OnConfigured()` returns `invalidParameter`, so the host's SET_CONFIGURATION is stalled and the device stays unconfigured. A stereo microphone with 193 samples is one case. The ring sizes are checked against the descriptors, not at compile time, because the channels come from the descriptors.

The microphone and the speaker share one function, because they share the Audio Control interface and the interface association. Splitting them would give two functions for one builder layout.

| Method | Context | Description |
|--------|---------|-------------|
| `WriteSamples(samples)` | Thread | Queues microphone samples, interleaved, and returns how many were taken. Returns 0 while the microphone is not streaming, so the ring never holds what the host would not hear |
| `ReadSamples(samples)` | Thread | Takes speaker samples the host sent and returns how many were copied |
| `GetWritableSamples()`, `GetReadableSamples()` | Thread | Free space of the microphone ring and samples waiting in the speaker ring |
| `IsStreaming(stream)`, `GetRate(stream)`, `GetVolume(stream)`, `IsMuted(stream)` | Thread | The state of a stream. The volume is in 1/256 dB |
| `GetCounters()` | Thread | The counters below, copied in a critical section |
| `SetStreamHandler(handler)`, `SetFeatureHandler(handler)`, `SetRateHandler(handler)` | Thread | The events. Return `busy` once the device is configured |

`USBAudioStream` is `Microphone` or `Speaker`. The counters are:

| Counter | Counts |
|---------|--------|
| `microphonePackets` | Microphone packets armed for the host. A packet the host misses still counts, since the function cannot see the miss |
| `microphoneUnderruns` | Microphone packets padded with silence, because the ring held fewer samples than the packet needs. A packet the port refuses is counted here too when it was padded |
| `microphoneArmRefusals` | Microphone packets the port refused to arm. Nothing is armed after one, so the stream stays idle until the next SET_INTERFACE alternate 1. A refused first arm inside a SET_INTERFACE fails that request instead, and the stream does not start |
| `speakerPackets` | Speaker packets received, including those the ring could not take in full, and those that were not whole frames |
| `speakerArmRefusals` | Speaker packets the port refused to arm. As for the microphone, the stream stays idle after one, and a refused first arm fails the SET_INTERFACE |
| `speakerPartialPackets` | Speaker packets that did not hold whole frames. The bytes after their last whole frame are dropped before the ring |
| `speakerOverrunSamples` | Speaker samples dropped because the ring was full |

The function needs no start-of-frame event. Its packets are armed by the alternate settings and by the completions, so the application does not wire the adapter's `onStartOfFrame`, and `Parameters::startOfFrameEvents` stays off.

### Microphone

`SET_INTERFACE` alternate 1 opens the IN endpoint and arms its first packet: rate / 1000 frames from the ring. The application cannot write before that, since `WriteSamples()` returns 0 while the stream is not running, so the first packet is silence and counts as an underrun.

That first packet is pending, not sent. The arming rule of the adapter applies (see [USBDeviceAdapter](../../../../../../Periphery/Adapter/USBDeviceAdapter.md#isochronous-endpoints)): a packet armed from inside its endpoint's completion callback goes out at the next host frame, and a packet armed from anywhere else, such as a `SET_INTERFACE`, goes out at the frame after that. On the port the first frame is answered with a zero-length packet and the packet goes out at the second frame. The simulator does the same.

Each completion, when the host has taken a packet, arms the next one from the ring. That packet goes out at the next frame, so it is ready before the host's next frame.

- A ring that runs short is padded with silence to the full packet. The host keeps its frame rate, and a short packet would make it count a different number of samples per frame. Each padded packet is an underrun.
- Nothing is taken from the ring while a packet is armed and not yet taken. The samples wait there, so a host that skips frames delays the stream and loses no samples, until the ring fills and `WriteSamples()` refuses more.
- A refused arm counts in `microphoneArmRefusals`, and its samples are dropped. Nothing is armed after it, so the stream stays idle until the next `SET_INTERFACE` alternate 1 arms a packet again.
- A refused first arm inside a `SET_INTERFACE` is different: the request fails, the core stalls it, the endpoint is closed again and no start is reported. The host then knows the stream did not start, and the stream is off.
- The samples are interleaved. The application writes whole frames: the function does not check the alignment of the samples, so a half frame shifts the channels of every later packet.
- Alternate 0 closes the endpoint, drops the packet armed for it and empties the ring. `WriteSamples()` returns 0 until the next alternate 1. A completion that arrives after the stream stopped is ignored.

### Speaker

`SET_INTERFACE` alternate 1 opens the OUT endpoint and arms a packet at once. Each completion copies the packet into the ring and arms the next one, so the next frame is accepted while the copy runs. A completion after the stream stopped is ignored.

- A packet that does not fit the ring is cut at the ring's end: the newest samples of it are dropped and counted in `speakerOverrunSamples`. The ring keeps the oldest samples, so playback continues from what arrived first.
- A packet that does not end on a whole frame keeps its whole frames. A stereo frame is four bytes, so a 3-byte packet gives no samples and a 6-byte packet gives two. The bytes after the last whole frame are dropped, and the packet counts in `speakerPartialPackets`, so the channels of the packets after it stay in place.
- A refused arm counts in `speakerArmRefusals`. The packet that came with the completion is still copied into the ring, and the stream stays idle after the refusal.
- A packet the port reports as overrun gives no samples, since the port reports 0 bytes for it.
- Alternate 0 closes the endpoint and empties the ring.

### Restart

`SET_INTERFACE` alternate 1 on a stream that is already streaming restarts it. USB 2.0 has `SET_INTERFACE` reset an endpoint's state, so the endpoint is closed and reopened, the ring is cleared and the first packet is armed again. The stream handler gets the stop and then the start, so a repeated alternate 1 is not a no-op.

The restart is also how a stream that a refused completion left idle is started again: the next `SET_INTERFACE` alternate 1 does it.

### Rates

The rate of a stream is the first one listed until the host sets another with SET_CUR (or until a bus reset). A change is reported once.

A SET_CUR to a different rate on a stream that is running empties that stream's ring, so the samples written at the old rate are neither sent nor played at the new one. A SET_CUR to the rate the stream already has changes nothing, and the ring keeps its samples. The counters are kept. The packet armed before the change is already in the port, so it goes out once more at the size it was armed with. The microphone's packets after it are sized at the new rate. The application writes at the new rate after the change.

### Requests

| Request | Target | Behaviour |
|---------|--------|-----------|
| GET_CUR, SET_CUR MUTE | Feature unit, master channel | One byte, 0 or 1. Another value is stalled |
| GET_CUR, SET_CUR VOLUME | Feature unit, master channel | Two bytes, 1/256 dB. The range is -60 dB (`0xC400`) to 0 dB (`0x0000`). A value outside it is stalled |
| GET_MIN, GET_MAX, GET_RES VOLUME | Feature unit | -60 dB, 0 dB and 1 dB. GET_MIN, GET_MAX and GET_RES of the mute are stalled |
| GET_CUR, SET_CUR SAMPLING_FREQ_CONTROL | Endpoint | Three bytes, little endian, one of the stream's rates. Stalled for a stream with one rate, which has no sampling control, and for a rate not listed |
| SET_INTERFACE, Audio Control interface | Interface 0 | Alternate 0 is accepted, as the base class accepts it. Any other alternate is stalled |
| Any other | | Stalled |

A control is served only on the master channel, and only when the feature unit has it. A request to a channel other than 0, to a unit that is not a feature unit, or to a control the unit does not have, is stalled.

A GET answers the first wLength bytes of its value when the host asks for less, and the whole value when it asks for more: the data stage then ends short, which the host takes as its end. A GET with wLength 0 is stalled. A SET_CUR takes exactly the length of its value, so any other length is stalled.

The function does not scale the samples. The application reads `GetVolume()` and `IsMuted()` and applies them to what it plays or records.

### Events

The stream, feature and rate handlers run in interrupt context, when the change happens. They must be short and must not block or allocate. They are set before the device starts. Each is called once per change, not per request: a SET_CUR to the value already set is served and reports nothing.

| Handler | Arguments | Called when |
|---------|-----------|-------------|
| `StreamDelegate` | stream, active | An alternate setting of a stream changes (a restart reports its stop and its start), and when a bus reset or deconfiguration stops it |
| `FeatureDelegate` | stream, volume, muted | The volume or the mute changes |
| `RateDelegate` | stream, rate | The sampling rate changes |

### Bus reset and deconfiguration

Both stop the streams and empty the rings. A bus reset also returns the volume and the mute to 0 dB and not muted, and the rate to its first listed value, reporting each change. A host sets them again after each enumeration. The counters are kept.

## Clocks

The microphone is synchronous: each packet holds exactly rate / 1000 frames, so the data rate is locked to the host's frames. The host's clock sets the rate, and the device must produce samples at that rate. A source with its own clock (an I2S or an ADC) drifts against the host's frames. A source that is slower than the host shows as underruns, and one that is faster as samples `WriteSamples()` refuses. The counters show the drift; the function does not correct it.

The speaker is adaptive: the host's packets set the rate, and the device takes what arrives into its ring and plays it at its own clock. An adaptive sink needs no feedback endpoint, but a clock that differs from the host's makes the ring fill or drain over time.

Asynchronous packet sizing for the microphone, and an asynchronous speaker that tells the host its rate through a feedback endpoint paced by the device's I2S clock, come later. They need that clock, and the feedback endpoint in the adapter's isochronous support.

## RAM and Packet Memory

| Item | Cost |
|------|------|
| Microphone ring | 2 bytes per sample of `MicrophoneSamples` |
| Speaker ring | 2 bytes per sample of `SpeakerSamples` |
| Packet buffers | 196 bytes each for the microphone and the speaker, 392 in all |
| Sample scratch | 98 samples of 16 bits for each stream, 392 bytes in all |
| Control and state | About 40 bytes |

With the rings of the example (192 and 128 samples), the function takes about 1.5 KB of RAM.

On the STM32G4 port, each isochronous endpoint takes two packet memory blocks, as the adapter's page gives them. For this device:

| Endpoint | Packet | Block | Cost |
|----------|--------|-------|------|
| Microphone IN | 98 | 128 | 256 |
| Speaker OUT | 196 | 224 | 448 |
| Total | | | 704 of the 832 bytes after the buffer table and endpoint 0 |

The rest, 128 bytes, is enough for one double-buffered bulk endpoint of 64 bytes, and nothing more. A device that adds bulk or interrupt endpoints must budget its packet memory against this. The microphone and the speaker must use different endpoint numbers (`0x81` and `0x02` in the example), since the two directions of a number share one register and the port refuses an isochronous IN beside an isochronous OUT of the same number.

### Packet Memory Budget

The G431 has 1024 bytes of packet memory. The fixed part is 192 bytes: the buffer table (64 bytes, whatever endpoints are open) and endpoint 0 (128 bytes). Each open isochronous endpoint takes two blocks of its packet size, and the port rounds a block up to 32 bytes above 62 bytes for both directions (see the [G4 page](../../../../../../Periphery/Adapter/Port/STM32/G4/USBDeviceAdapterG4.md#packet-memory-cost)). The packet of a path is sized by its highest listed rate, as the descriptor table above shows.

| Layout | Microphone: packet, block, cost | Speaker: packet, block, cost | Total | Fits 1024 |
|--------|----------------------------------|------------------------------|-------|-----------|
| USBAudio demo: mono microphone, stereo speaker, 48 kHz | 98, 128, 256 | 196, 224, 448 | 896 | Yes, 128 left |
| `BasicUsb` and `NamedUsb` (test devices): the same paths | 98, 128, 256 | 196, 224, 448 | 896 | Yes |
| `MultiRateUsb`: mono microphone listing 16, 32 and 48 kHz, stereo speaker listing 48 and 16 kHz | 98, 128, 256 | 196, 224, 448 | 896 | Yes |
| `StereoMicUsb`: stereo microphone, mono speaker, 48 kHz | 196, 224, 448 | 98, 128, 256 | 896 | Yes |
| Stereo microphone and stereo speaker, 16 kHz | 68, 96, 192 | 68, 96, 192 | 576 | Yes |
| `StereoPairUsb`: stereo microphone and stereo speaker, 48 kHz | 196, 224, 448 | 196, 224, 448 | 1088 | No |

The limit is two stereo paths at 48 kHz. Their packets are 196 bytes, so each endpoint takes 448 bytes, and the pair needs 1088 bytes. The second `SET_INTERFACE` is then refused: the port answers `outOfMemory`, and the core stalls the request, so that stream stays off while the other one streams. The builder accepts the layout, because it does not place packet memory. Any other layout in the table fits. `TestAudioStreaming.cpp` (`TestStereoPairRunsOutOfPacketMemory`, `TestStereoPairSpeakerFirstRefusesMicrophone`) and `TestFsdevPort.cpp` (`TestIsoStereoPairOutOfMemory`) pin the refusal.

## Host Support

| Host | Status |
|------|--------|
| Windows 11 | The class is UAC 1.0, bound by the in-box `usbaudio.sys` without a driver. Measured on the STM32G431: both directions stream at one packet per frame, 1000 packets per second each way. The microphone had one underrun, at stream start, and the speaker no error |
| Linux | The class is bound by `snd-usb-audio` without a driver. Not tested on hardware |
| macOS | Bound by its in-box class driver without a driver. Not tested on hardware |

The descriptors are checked against the specification in the host tests. Windows 11 is the only host that has streamed the device. The zero-length packet of the first frame after `SET_INTERFACE` and the one-frame bound on the interrupt are not measured yet (see the [adapter page](../../../../../../Periphery/Adapter/USBDeviceAdapter.md#measured-on-hardware)).

## Limits

| Item | Value |
|------|-------|
| Channels | 1 or 2 per path |
| Sampling rates | 16000, 32000 and 48000 Hz |
| Sample format | 16-bit PCM, two bytes per subframe |
| Feature controls | Mute and volume on the master channel, per path |
| Sampling control | Only with more than one rate; a single rate has none |
| Microphone clock | Synchronous: the host's frames. No asynchronous sizing yet |
| Speaker clock | Adaptive. No feedback endpoint |
| Volume | Reported, not applied to the samples |
| Alternate settings | 0 and 1 per stream |
| Packet size | Fixed by the highest rate of the path, 98 or 196 bytes |
| Listed rates | Each must fit the endpoint's wMaxPacketSize: the microphone's rate / 1000 frames of samples, the speaker's one frame more. A descriptor that lists a rate its endpoint cannot carry is refused at configuration (`invalidParameter`) |
| Packet memory | Two stereo paths at 48 kHz need 1088 bytes of the G431's 1024. See the packet memory budget |
| Speed | Full |

## Tests

`.tool/sim` runs the class against the simulated controller, with the expected bytes written by hand:

- `TestAudioDescriptors.cpp`: the configuration of a mono microphone and a stereo speaker, and of a three-rate microphone with a two-rate speaker, byte for byte; the layout the function receives; the names; a hand-written microphone and speaker whose endpoint is too small for a listed rate, refused at configuration, and the same with the endpoint big enough, accepted
- `TestAudioEndpointDescriptors.cpp`: the standard and the 9-byte endpoint forms, the isochronous forms with and without feedback
- `TestAudioRequests.cpp`: mute and volume, the range and its stalls, unknown controls and channels, the sampling frequency with one rate and with several, an unknown request, and the GET lengths (shorter, longer, zero) with the exact length of a SET
- `TestAudioStreaming.cpp`: the alternates and the Audio Control interface's alternate, the microphone's first packet and each completion's packet, the underrun, a packet waiting for the host, the stop and its restart, a repeated alternate 1 that restarts the stream, the speaker's packets and its overflow, a speaker packet that is not whole frames, a rate change and a sampling change that empties the ring, a repeated rate that keeps it, a bus reset, a deconfiguration, a late completion, a refused arm (microphone and speaker, in a completion and as the first arm of a SET_INTERFACE), a refused arm restarted by the next SET_INTERFACE, a ring of partial frames refused at configuration, two stereo 48 kHz paths where the second SET_INTERFACE is refused for packet memory

The builder's compile-time rules are checked by the snippets in `.tool/sim/BuilderErrors/audio_*.cpp`.

## Compile Errors

| Error | Cause |
|-------|-------|
| `an audio function has a microphone or a speaker` | The chain has neither path |
| `a path has 1 or 2 channels` | A channel count of 0, 3 or more |
| `a sample rate is 16000, 32000 or 48000` | An unsupported rate |
| `a sample rate is listed once` | A repeated rate |
| `MicrophoneRate needs Microphone() first` | A rate, volume, mute, name or address given before its path |
| `MicrophoneAddress needs an IN address (bit 7 set)` | A microphone endpoint with an OUT address |
| `SpeakerAddress needs an OUT address (bit 7 clear)` | A speaker endpoint with an IN address |
