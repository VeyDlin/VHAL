#include <VHAL.h>
#include "AudioTestDevices.h"
#include "SimAssert.h"
#include "SimHost.h"
#include <Drivers/Interface/USB/Class/Audio/AudioFunction.h>
#include <Drivers/Interface/USB/Class/Audio/USBAudioBuilder.h>
#include <array>
#include <cstddef>
#include <span>


// The streams of the audio function at the adapter: the alternate settings open and close the isochronous endpoints and arm the first
// packet, the completion of each microphone packet arms the next, the speaker receives one packet per frame into its ring, and the rate
// changes the packet size. Each frame is one host token (IsoInFrame() or IsoOutFrame()), and its completion runs the function.


namespace {


using Basic = AudioTest::AudioFixture<AudioTest::BasicUsb>;
using MultiRate = AudioTest::AudioFixture<AudioTest::MultiRateUsb>;
// The rings that do not hold whole frames: a stereo speaker with 129 samples, and a stereo microphone with 193.
using OddSpeaker = AudioTest::AudioFixture<AudioTest::BasicUsb, AudioFunction<192, 129>>;
using OddMic = AudioTest::AudioFixture<AudioTest::StereoMicUsb, AudioFunction<193, 128>>;

constexpr uint8 MicInterface = 1;
constexpr uint8 SpkInterface = 2;
constexpr uint8 MicAddress = 0x81;
constexpr uint8 SpkAddress = 0x02;


// A context that counts the calls a function makes on it. A completion handed to a function after its stream stopped must start nothing,
// so the count shows whether the function answered it.
struct CountingContext : public USBDeviceContext {
	uint32 calls = 0;


	ResultStatus OpenEndpoint(const USBDeviceAdapter::EndpointConfig&) override {
		calls++;
		return ResultStatus::notReady;
	}


	ResultStatus CloseEndpoint(uint8) override {
		calls++;
		return ResultStatus::notReady;
	}


	ResultStatus StartInTransfer(uint8, std::span<const uint8>, bool) override {
		calls++;
		return ResultStatus::notReady;
	}


	ResultStatus StartOutTransfer(uint8, std::span<uint8>) override {
		calls++;
		return ResultStatus::notReady;
	}


	ResultStatus Stall(uint8) override {
		calls++;
		return ResultStatus::notReady;
	}


	ResultStatus ClearStall(uint8) override {
		calls++;
		return ResultStatus::notReady;
	}


	ResultStatus RemoteWakeup() override {
		calls++;
		return ResultStatus::notReady;
	}


	ResultStatus ControlSendData(std::span<const uint8>) override {
		calls++;
		return ResultStatus::notReady;
	}


	ResultStatus ControlReceiveData(std::span<uint8>) override {
		calls++;
		return ResultStatus::notReady;
	}
};


// A context that opens and closes endpoints, and refuses every transfer, as a port that cannot arm would. It counts the endpoints it opens
// and closes, and the transfers it refuses.
struct RefusingArmContext : public USBDeviceContext {
	uint32 opened = 0;
	uint32 closed = 0;
	uint32 refusedTransfers = 0;


	ResultStatus OpenEndpoint(const USBDeviceAdapter::EndpointConfig&) override {
		opened++;
		return ResultStatus::ok;
	}


	ResultStatus CloseEndpoint(uint8) override {
		closed++;
		return ResultStatus::ok;
	}


	ResultStatus StartInTransfer(uint8, std::span<const uint8>, bool) override {
		refusedTransfers++;
		return ResultStatus::notReady;
	}


	ResultStatus StartOutTransfer(uint8, std::span<uint8>) override {
		refusedTransfers++;
		return ResultStatus::notReady;
	}


	ResultStatus Stall(uint8) override {
		return ResultStatus::notReady;
	}


	ResultStatus ClearStall(uint8) override {
		return ResultStatus::notReady;
	}


	ResultStatus RemoteWakeup() override {
		return ResultStatus::notReady;
	}


	ResultStatus ControlSendData(std::span<const uint8>) override {
		return ResultStatus::notReady;
	}


	ResultStatus ControlReceiveData(std::span<uint8>) override {
		return ResultStatus::notReady;
	}
};


}


// The microphone's alternate 1 opens its isochronous IN endpoint (98 bytes) and arms its first packet at once. Alternate 0 closes it. Alternate
// 2 is not listed, so it is stalled. The speaker's endpoint opens with 196 bytes and an OUT packet armed at once.
void TestAlternatesOpenAndCloseEndpoints() {
	Basic fx;
	fx.Start();
	SIM_EXPECT(fx.sim.FindEndpoint(MicAddress) == nullptr);

	SIM_EXPECT_EQ(fx.SetInterface(MicInterface, 1), ResultStatus::ok);
	const USBDeviceAdapter::EndpointConfig* mic = fx.sim.FindEndpoint(MicAddress);
	SIM_EXPECT(mic != nullptr);
	if (mic != nullptr) {
		SIM_EXPECT(mic->type == USBEndpointType::Isochronous);
		SIM_EXPECT_EQ(mic->maxPacketSize, 98);
	}

	SIM_EXPECT(fx.audio.IsStreaming(USBAudioStream::Microphone));
	SIM_EXPECT(fx.sim.HasArmedTransfer(MicAddress));
	SIM_EXPECT_EQ(fx.audio.GetCounters().microphonePackets, 1U);
	SIM_EXPECT_EQ(fx.audio.GetWritableSamples(), 192U);
	SIM_EXPECT_EQ(fx.recorder.streams.count, 1U);
	if (fx.recorder.streams.count == 1) {
		SIM_EXPECT(fx.recorder.streams[0].stream == USBAudioStream::Microphone);
		SIM_EXPECT(fx.recorder.streams[0].active);
	}

	// A second alternate 1 restarts the stream: the stop and the start are both reported, and a new first packet is armed.
	SIM_EXPECT_EQ(fx.SetInterface(MicInterface, 1), ResultStatus::ok);
	SIM_EXPECT(fx.sim.HasArmedTransfer(MicAddress));
	SIM_EXPECT_EQ(fx.recorder.streams.count, 3U);
	if (fx.recorder.streams.count == 3) {
		SIM_EXPECT(!fx.recorder.streams[1].active);
		SIM_EXPECT(fx.recorder.streams[2].active);
	}

	SIM_EXPECT_EQ(fx.audio.GetCounters().microphonePackets, 2U);

	SIM_EXPECT_EQ(fx.SetInterface(MicInterface, 2), ResultStatus::accessError);
	SIM_EXPECT(fx.audio.IsStreaming(USBAudioStream::Microphone));

	SIM_EXPECT_EQ(fx.SetInterface(MicInterface, 0), ResultStatus::ok);
	SIM_EXPECT(fx.sim.FindEndpoint(MicAddress) == nullptr);
	SIM_EXPECT(!fx.sim.HasArmedTransfer(MicAddress));
	SIM_EXPECT(!fx.audio.IsStreaming(USBAudioStream::Microphone));
	SIM_EXPECT_EQ(fx.recorder.streams.count, 4U);
	if (fx.recorder.streams.count == 4) {
		SIM_EXPECT(!fx.recorder.streams[3].active);
	}

	SIM_EXPECT_EQ(fx.SetInterface(SpkInterface, 1), ResultStatus::ok);
	const USBDeviceAdapter::EndpointConfig* speaker = fx.sim.FindEndpoint(SpkAddress);
	SIM_EXPECT(speaker != nullptr);
	if (speaker != nullptr) {
		SIM_EXPECT(speaker->type == USBEndpointType::Isochronous);
		SIM_EXPECT_EQ(speaker->maxPacketSize, 196);
	}

	SIM_EXPECT(fx.sim.HasArmedTransfer(SpkAddress));
	SIM_EXPECT(fx.audio.IsStreaming(USBAudioStream::Speaker));
}


// SET_INTERFACE on the Audio Control interface (0): alternate 0 is accepted, as the base class accepts it, and any other alternate is
// stalled. Neither changes a stream.
void TestControlInterfaceAcceptsAlternateZeroOnly() {
	Basic fx;
	fx.Start();

	SIM_EXPECT_EQ(fx.SetInterface(0, 0), ResultStatus::ok);
	SIM_EXPECT_EQ(fx.SetInterface(0, 1), ResultStatus::accessError);
	SIM_EXPECT_EQ(fx.device.GetState(), USBDeviceState::Configured);
	SIM_EXPECT_EQ(fx.recorder.streams.count, 0U);
	SIM_EXPECT(!fx.audio.IsStreaming(USBAudioStream::Microphone));
	SIM_EXPECT(!fx.audio.IsStreaming(USBAudioStream::Speaker));
}


// The first packet is armed by SET_INTERFACE, before the application can write, so it is silence and an underrun. It is pending, so the first
// host frame is a zero-length packet, and the silence goes out at the second. Each completion arms the next packet from the ring. Frame A is
// sent by the third host frame and frame B by the fourth.
void TestMicrophoneSendsOnePacketPerFrame() {
	Basic fx;
	fx.Start();
	SIM_EXPECT_EQ(fx.SetInterface(MicInterface, 1), ResultStatus::ok);

	std::array<int16, 48> frameA{};
	std::array<int16, 48> frameB{};
	for (std::size_t index = 0; index < frameA.size(); index++) {
		frameA[index] = AudioTest::PatternSample(1, index);
		frameB[index] = AudioTest::PatternSample(2, index);
	}

	SIM_EXPECT_EQ(fx.audio.WriteSamples(frameA), 48);
	SIM_EXPECT_EQ(fx.audio.WriteSamples(frameB), 48);

	std::array<uint8, 256> in{};
	uint32 length = 0;
	std::array<int16, 48> silence{};
	std::array<uint8, 96> silentBytes = AudioTest::SamplesToBytes(silence);

	// The packet of SET_INTERFACE is pending: this frame is a zero-length packet with no completion, and it writes that packet.
	SIM_EXPECT(fx.sim.IsoInFrame(MicAddress, in, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, 0U);
	SIM_EXPECT_EQ(fx.audio.GetCounters().microphoneUnderruns, 1U);

	// The silence goes out at the second frame, and its completion arms frame A from the ring.
	SIM_EXPECT(fx.sim.IsoInFrame(MicAddress, in, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, 96U);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(96), silentBytes));

	std::array<uint8, 96> expectedA = AudioTest::SamplesToBytes(frameA);
	SIM_EXPECT(fx.sim.IsoInFrame(MicAddress, in, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, 96U);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(96), expectedA));

	std::array<uint8, 96> expectedB = AudioTest::SamplesToBytes(frameB);
	SIM_EXPECT(fx.sim.IsoInFrame(MicAddress, in, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, 96U);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(96), expectedB));

	// Four packets armed: the one from SET_INTERFACE, one for each of the three completions. Two were padded: the first, and the one armed
	// after frame B, when the ring was empty.
	USBAudioCounters counters = fx.audio.GetCounters();
	SIM_EXPECT_EQ(counters.microphonePackets, 4U);
	SIM_EXPECT_EQ(counters.microphoneUnderruns, 2U);
	SIM_EXPECT_EQ(counters.microphoneArmRefusals, 0U);
}


// A ring that holds fewer samples than a packet needs is padded with silence: ten samples, then 38 zero samples. Each packet armed with a short
// ring, or with an empty one, is an underrun.
void TestMicrophoneUnderrunIsSilence() {
	Basic fx;
	fx.Start();
	SIM_EXPECT_EQ(fx.SetInterface(MicInterface, 1), ResultStatus::ok);

	std::array<int16, 48> partial{};
	for (std::size_t index = 0; index < 10; index++) {
		partial[index] = AudioTest::PatternSample(3, index);
	}

	SIM_EXPECT_EQ(fx.audio.WriteSamples(std::span<const int16>(partial).first(10)), 10);

	std::array<uint8, 256> in{};
	uint32 length = 0;
	std::array<int16, 48> silence{};
	std::array<uint8, 96> silentBytes = AudioTest::SamplesToBytes(silence);

	// The packet of SET_INTERFACE is pending, so the first frame is a zero-length packet, and the silence goes out at the second.
	SIM_EXPECT(fx.sim.IsoInFrame(MicAddress, in, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, 0U);
	SIM_EXPECT(fx.sim.IsoInFrame(MicAddress, in, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, 96U);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(96), silentBytes));

	// The completion of the silence armed the ten samples, padded with 38 zeros.
	std::array<int16, 48> paddedFrame{};
	for (std::size_t index = 0; index < 10; index++) {
		paddedFrame[index] = AudioTest::PatternSample(3, index);
	}

	std::array<uint8, 96> expected = AudioTest::SamplesToBytes(paddedFrame);
	SIM_EXPECT(fx.sim.IsoInFrame(MicAddress, in, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, 96U);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(96), expected));

	// The ring is empty now, so the next packet is silence in full.
	SIM_EXPECT(fx.sim.IsoInFrame(MicAddress, in, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, 96U);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(96), silentBytes));

	// Armed packets: the one from SET_INTERFACE and one per completion, four in all. Each one was padded.
	USBAudioCounters counters = fx.audio.GetCounters();
	SIM_EXPECT_EQ(counters.microphonePackets, 4U);
	SIM_EXPECT_EQ(counters.microphoneUnderruns, 4U);
}


// A packet armed and not yet taken holds the ring's samples until the host takes it: nothing is popped while no frame comes. Then each host
// frame takes the armed packet and arms the next one from the ring, in order.
void TestArmedPacketWaitsForTheHost() {
	Basic fx;
	fx.Start();
	SIM_EXPECT_EQ(fx.SetInterface(MicInterface, 1), ResultStatus::ok);

	std::array<int16, 48> frameA{};
	std::array<int16, 48> frameB{};
	std::array<int16, 48> frameC{};
	for (std::size_t index = 0; index < 48; index++) {
		frameA[index] = AudioTest::PatternSample(4, index);
		frameB[index] = AudioTest::PatternSample(5, index);
		frameC[index] = AudioTest::PatternSample(6, index);
	}

	SIM_EXPECT_EQ(fx.audio.WriteSamples(frameA), 48);
	SIM_EXPECT_EQ(fx.audio.WriteSamples(frameB), 48);
	SIM_EXPECT_EQ(fx.audio.WriteSamples(frameC), 48);
	SIM_EXPECT_EQ(fx.audio.GetWritableSamples(), 48U);

	std::array<uint8, 256> in{};
	uint32 length = 0;
	std::array<uint8, 96> bytesA = AudioTest::SamplesToBytes(frameA);
	std::array<uint8, 96> bytesB = AudioTest::SamplesToBytes(frameB);
	std::array<int16, 48> silence{};
	std::array<uint8, 96> silentBytes = AudioTest::SamplesToBytes(silence);

	// The packet of SET_INTERFACE is pending: the first frame is a zero-length packet, and it takes nothing from the ring.
	SIM_EXPECT(fx.sim.IsoInFrame(MicAddress, in, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, 0U);
	SIM_EXPECT_EQ(fx.audio.GetWritableSamples(), 48U);

	SIM_EXPECT(fx.sim.IsoInFrame(MicAddress, in, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, 96U);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(96), silentBytes));
	SIM_EXPECT_EQ(fx.audio.GetWritableSamples(), 96U);

	SIM_EXPECT(fx.sim.IsoInFrame(MicAddress, in, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, 96U);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(96), bytesA));
	SIM_EXPECT_EQ(fx.audio.GetWritableSamples(), 144U);

	SIM_EXPECT(fx.sim.IsoInFrame(MicAddress, in, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, 96U);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(96), bytesB));
	SIM_EXPECT_EQ(fx.audio.GetWritableSamples(), 192U);

	// Armed packets: SET_INTERFACE's, then the completions after each of the three frames. Only the first was padded.
	USBAudioCounters counters = fx.audio.GetCounters();
	SIM_EXPECT_EQ(counters.microphonePackets, 4U);
	SIM_EXPECT_EQ(counters.microphoneUnderruns, 1U);
	SIM_EXPECT_EQ(counters.microphoneArmRefusals, 0U);
}


// Alternate 0 closes the endpoint, drops the packet armed for it, empties the ring and refuses new samples until the microphone is selected
// again.
void TestStoppingEmptiesRingAndRefusesSamples() {
	Basic fx;
	fx.Start();
	SIM_EXPECT_EQ(fx.SetInterface(MicInterface, 1), ResultStatus::ok);

	std::array<int16, 48> frame{};
	SIM_EXPECT_EQ(fx.audio.WriteSamples(frame), 48);
	SIM_EXPECT_EQ(fx.audio.GetWritableSamples(), 144U);

	SIM_EXPECT_EQ(fx.SetInterface(MicInterface, 0), ResultStatus::ok);
	SIM_EXPECT(!fx.sim.HasArmedTransfer(MicAddress));
	SIM_EXPECT_EQ(fx.audio.GetWritableSamples(), 192U);
	SIM_EXPECT_EQ(fx.audio.WriteSamples(frame), 0);
	SIM_EXPECT_EQ(fx.audio.GetWritableSamples(), 192U);
}


// A microphone stopped and started again: the packet armed by the first selection is gone, and the second selection arms a new one. The
// samples written while the stream was stopped were refused, so the host hears silence first and then the frame written after the restart.
void TestMicrophoneRestartAfterStop() {
	Basic fx;
	fx.Start();
	SIM_EXPECT_EQ(fx.SetInterface(MicInterface, 1), ResultStatus::ok);
	SIM_EXPECT(fx.sim.HasArmedTransfer(MicAddress));

	SIM_EXPECT_EQ(fx.SetInterface(MicInterface, 0), ResultStatus::ok);
	SIM_EXPECT(fx.sim.FindEndpoint(MicAddress) == nullptr);
	SIM_EXPECT(!fx.sim.HasArmedTransfer(MicAddress));

	std::array<int16, 48> frameA{};
	for (std::size_t index = 0; index < frameA.size(); index++) {
		frameA[index] = AudioTest::PatternSample(14, index);
	}

	SIM_EXPECT_EQ(fx.audio.WriteSamples(frameA), 0);

	SIM_EXPECT_EQ(fx.SetInterface(MicInterface, 1), ResultStatus::ok);
	SIM_EXPECT(fx.sim.HasArmedTransfer(MicAddress));
	SIM_EXPECT_EQ(fx.audio.GetWritableSamples(), 192U);
	SIM_EXPECT_EQ(fx.audio.WriteSamples(frameA), 48);

	std::array<uint8, 256> in{};
	uint32 length = 0;
	std::array<int16, 48> silence{};
	std::array<uint8, 96> silentBytes = AudioTest::SamplesToBytes(silence);
	std::array<uint8, 96> bytesA = AudioTest::SamplesToBytes(frameA);
	// The packet of the second selection is pending, so the first frame is a zero-length packet and the silence goes out at the second.
	SIM_EXPECT(fx.sim.IsoInFrame(MicAddress, in, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, 0U);
	SIM_EXPECT(fx.sim.IsoInFrame(MicAddress, in, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, 96U);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(96), silentBytes));

	SIM_EXPECT(fx.sim.IsoInFrame(MicAddress, in, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, 96U);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(96), bytesA));

	// Two selections armed two packets. The completion of the silence armed frame A, and the completion of frame A found the ring empty and
	// armed silence: four packets, three padded (the two selections and the last one).
	USBAudioCounters counters = fx.audio.GetCounters();
	SIM_EXPECT_EQ(counters.microphonePackets, 4U);
	SIM_EXPECT_EQ(counters.microphoneUnderruns, 3U);
}


// The speaker's packet of 192 bytes (96 stereo samples) goes into the ring, the endpoint is armed again, and the application reads the
// samples in the order they arrived.
void TestSpeakerReceivesPackets() {
	Basic fx;
	fx.Start();
	SIM_EXPECT_EQ(fx.SetInterface(SpkInterface, 1), ResultStatus::ok);
	SIM_EXPECT(fx.sim.HasArmedTransfer(SpkAddress));

	std::array<int16, 96> samples{};
	for (std::size_t index = 0; index < samples.size(); index++) {
		samples[index] = AudioTest::PatternSample(7, index);
	}

	std::array<uint8, 192> packet = AudioTest::SamplesToBytes(samples);
	SIM_EXPECT(fx.sim.IsoOutFrame(SpkAddress, packet) == ResultStatus::ok);
	SIM_EXPECT_EQ(fx.audio.GetReadableSamples(), 96U);
	SIM_EXPECT(fx.sim.HasArmedTransfer(SpkAddress));

	std::array<int16, 96> out{};
	SIM_EXPECT_EQ(fx.audio.ReadSamples(out), 96U);
	bool same = true;
	for (std::size_t index = 0; index < samples.size(); index++) {
		if (out[index] != samples[index]) {
			same = false;
		}
	}

	SIM_EXPECT(same);
	SIM_EXPECT_EQ(fx.audio.GetReadableSamples(), 0U);

	USBAudioCounters counters = fx.audio.GetCounters();
	SIM_EXPECT_EQ(counters.speakerPackets, 1U);
	SIM_EXPECT_EQ(counters.speakerOverrunSamples, 0U);
}


// The speaker's ring holds 128 samples. Two packets of 96 fill it: the first is whole, and the second keeps 32 of its samples and drops
// the newest 64. A third packet into the full ring drops all 96. The ring keeps the oldest samples.
void TestSpeakerOverflowDropsNewestSamples() {
	Basic fx;
	fx.Start();
	SIM_EXPECT_EQ(fx.SetInterface(SpkInterface, 1), ResultStatus::ok);

	std::array<int16, 96> first{};
	std::array<int16, 96> second{};
	for (std::size_t index = 0; index < 96; index++) {
		first[index] = AudioTest::PatternSample(8, index);
		second[index] = AudioTest::PatternSample(9, index);
	}

	std::array<uint8, 192> firstBytes = AudioTest::SamplesToBytes(first);
	std::array<uint8, 192> secondBytes = AudioTest::SamplesToBytes(second);
	SIM_EXPECT(fx.sim.IsoOutFrame(SpkAddress, firstBytes) == ResultStatus::ok);
	SIM_EXPECT(fx.sim.IsoOutFrame(SpkAddress, secondBytes) == ResultStatus::ok);
	SIM_EXPECT_EQ(fx.audio.GetReadableSamples(), 128U);

	USBAudioCounters counters = fx.audio.GetCounters();
	SIM_EXPECT_EQ(counters.speakerPackets, 2U);
	SIM_EXPECT_EQ(counters.speakerOverrunSamples, 64U);

	SIM_EXPECT(fx.sim.IsoOutFrame(SpkAddress, secondBytes) == ResultStatus::ok);
	counters = fx.audio.GetCounters();
	SIM_EXPECT_EQ(counters.speakerPackets, 3U);
	SIM_EXPECT_EQ(counters.speakerOverrunSamples, 160U);

	std::array<int16, 128> out{};
	SIM_EXPECT_EQ(fx.audio.ReadSamples(out), 128U);
	bool firstSame = true;
	for (std::size_t index = 0; index < 96; index++) {
		if (out[index] != first[index]) {
			firstSame = false;
		}
	}

	bool secondSame = true;
	for (std::size_t index = 0; index < 32; index++) {
		if (out[96 + index] != second[index]) {
			secondSame = false;
		}
	}

	SIM_EXPECT(firstSame);
	SIM_EXPECT(secondSame);
}


// The rate changes the microphone's packet size: at 16 kHz a packet is 16 samples (32 bytes), at 48 kHz 48 samples (96 bytes), and at 32 kHz
// 32 samples (64 bytes). The packet armed before a change is already in the port, so it goes out once at the size it was armed with. Each
// change is reported once, and the ring is emptied by it, so the frames written after a change are the ones sent at the new rate.
void TestRateChangeChangesPacketSize() {
	MultiRate fx;
	fx.Start();
	SIM_EXPECT_EQ(fx.SetInterface(MicInterface, 1), ResultStatus::ok);
	SIM_EXPECT_EQ(fx.audio.GetRate(USBAudioStream::Microphone), 16000U);

	std::array<int16, 16> frame16{};
	std::array<int16, 48> frame48{};
	std::array<int16, 32> frame32{};
	for (std::size_t index = 0; index < 16; index++) {
		frame16[index] = AudioTest::PatternSample(10, index);
	}

	for (std::size_t index = 0; index < 48; index++) {
		frame48[index] = AudioTest::PatternSample(11, index);
	}

	for (std::size_t index = 0; index < 32; index++) {
		frame32[index] = AudioTest::PatternSample(12, index);
	}

	std::array<uint8, 256> in{};
	uint32 length = 0;
	std::array<uint8, 32> bytes16 = AudioTest::SamplesToBytes(frame16);
	std::array<uint8, 96> bytes48 = AudioTest::SamplesToBytes(frame48);
	std::array<uint8, 64> bytes32 = AudioTest::SamplesToBytes(frame32);
	SIM_EXPECT_EQ(fx.audio.WriteSamples(frame16), 16);

	// The packet armed by SET_INTERFACE is pending, so the first frame is a zero-length packet, and nothing is taken from the ring by it.
	SIM_EXPECT(fx.sim.IsoInFrame(MicAddress, in, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, 0U);
	SIM_EXPECT_EQ(fx.audio.GetWritableSamples(), 176U);

	// The second frame sends the first packet, which is silence of 16 samples. Its completion arms frame 16 at the 16 kHz size.
	SIM_EXPECT(fx.sim.IsoInFrame(MicAddress, in, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, 32U);
	std::array<int16, 16> silence16{};
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(32), AudioTest::SamplesToBytes(silence16)));

	const std::array<uint8, 3> hz48000 = { 0x80, 0xBB, 0x00 };
	SIM_EXPECT_EQ(fx.SamplingSet(MicAddress, hz48000), ResultStatus::ok);
	SIM_EXPECT_EQ(fx.audio.GetRate(USBAudioStream::Microphone), 48000U);
	SIM_EXPECT_EQ(fx.audio.WriteSamples(frame48), 48);

	// Frame 16 was armed at 16 kHz, so the port sends it at that size. The completion arms frame 48 at 48 kHz.
	SIM_EXPECT(fx.sim.IsoInFrame(MicAddress, in, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, 32U);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(32), bytes16));

	SIM_EXPECT(fx.sim.IsoInFrame(MicAddress, in, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, 96U);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(96), bytes48));

	// The completion of that frame found the ring empty, so it armed silence of 48 samples. It goes out at the next frame.
	const std::array<uint8, 3> hz32000 = { 0x00, 0x7D, 0x00 };
	SIM_EXPECT(fx.sim.IsoInFrame(MicAddress, in, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, 96U);
	std::array<int16, 48> silence48{};
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(96), AudioTest::SamplesToBytes(silence48)));

	SIM_EXPECT_EQ(fx.SamplingSet(MicAddress, hz32000), ResultStatus::ok);
	SIM_EXPECT_EQ(fx.audio.GetRate(USBAudioStream::Microphone), 32000U);
	SIM_EXPECT_EQ(fx.audio.WriteSamples(frame32), 32);

	// The silence armed at 48 kHz goes out once more at its size. The completion arms frame 32 at 32 kHz.
	SIM_EXPECT(fx.sim.IsoInFrame(MicAddress, in, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, 96U);

	// Frame 32 goes out at 32 kHz, and the completion finds the ring empty and arms silence of 32 samples.
	SIM_EXPECT(fx.sim.IsoInFrame(MicAddress, in, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, 64U);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(64), bytes32));

	// Armed packets: SET_INTERFACE's and one per completion, seven in all. Four were padded: SET_INTERFACE's, and three that found the ring
	// empty, after the token that sent frame 48, after the silence that followed it, and after frame 32.
	USBAudioCounters counters = fx.audio.GetCounters();
	SIM_EXPECT_EQ(counters.microphonePackets, 7U);
	SIM_EXPECT_EQ(counters.microphoneUnderruns, 4U);

	SIM_EXPECT_EQ(fx.recorder.rates.count, 2U);
	if (fx.recorder.rates.count == 2) {
		SIM_EXPECT_EQ(fx.recorder.rates[0].rate, 48000U);
		SIM_EXPECT_EQ(fx.recorder.rates[1].rate, 32000U);
	}
}


// A sampling-frequency SET_CUR on a streaming path empties that path's ring, so samples written at the old rate are neither sent nor played at
// the new one. The counters are kept. The speaker's ring is emptied the same way.
void TestSamplingChangeClearsStreamingRing() {
	MultiRate fx;
	fx.Start();
	SIM_EXPECT_EQ(fx.SetInterface(MicInterface, 1), ResultStatus::ok);

	std::array<int16, 16> frame{};
	for (std::size_t index = 0; index < frame.size(); index++) {
		frame[index] = AudioTest::PatternSample(13, index);
	}

	SIM_EXPECT_EQ(fx.audio.WriteSamples(frame), 16);
	SIM_EXPECT_EQ(fx.audio.GetWritableSamples(), 176U);

	const std::array<uint8, 3> hz32000 = { 0x00, 0x7D, 0x00 };
	SIM_EXPECT_EQ(fx.SamplingSet(MicAddress, hz32000), ResultStatus::ok);
	SIM_EXPECT_EQ(fx.audio.GetRate(USBAudioStream::Microphone), 32000U);
	SIM_EXPECT_EQ(fx.audio.GetWritableSamples(), 192U);
	USBAudioCounters counters = fx.audio.GetCounters();
	SIM_EXPECT_EQ(counters.microphonePackets, 1U);
	SIM_EXPECT_EQ(counters.microphoneUnderruns, 1U);

	SIM_EXPECT_EQ(fx.SetInterface(SpkInterface, 1), ResultStatus::ok);
	std::array<int16, 96> samples{};
	for (std::size_t index = 0; index < samples.size(); index++) {
		samples[index] = AudioTest::PatternSample(15, index);
	}

	std::array<uint8, 192> packet = AudioTest::SamplesToBytes(samples);
	SIM_EXPECT(fx.sim.IsoOutFrame(SpkAddress, packet) == ResultStatus::ok);
	SIM_EXPECT_EQ(fx.audio.GetReadableSamples(), 96U);

	const std::array<uint8, 3> hz16000 = { 0x80, 0x3E, 0x00 };
	SIM_EXPECT_EQ(fx.SamplingSet(SpkAddress, hz16000), ResultStatus::ok);
	SIM_EXPECT_EQ(fx.audio.GetRate(USBAudioStream::Speaker), 16000U);
	SIM_EXPECT_EQ(fx.audio.GetReadableSamples(), 0U);
	counters = fx.audio.GetCounters();
	SIM_EXPECT_EQ(counters.speakerPackets, 1U);
	SIM_EXPECT_EQ(counters.speakerOverrunSamples, 0U);

	SIM_EXPECT_EQ(fx.recorder.rates.count, 2U);
	if (fx.recorder.rates.count == 2) {
		SIM_EXPECT(fx.recorder.rates[0].stream == USBAudioStream::Microphone);
		SIM_EXPECT(fx.recorder.rates[1].stream == USBAudioStream::Speaker);
	}
}


// Alternate 1 on a stream with a rate change, a bus reset and a deconfiguration: each stops or resets the streams and empties the rings, and the
// counters survive them.
void TestBusResetStopsStreamsAndRestoresDefaults() {
	Basic fx;
	fx.Start();
	SIM_EXPECT_EQ(fx.SetInterface(MicInterface, 1), ResultStatus::ok);
	SIM_EXPECT_EQ(fx.SetInterface(SpkInterface, 1), ResultStatus::ok);

	const std::array<uint8, 1> muteOn = { 1 };
	const std::array<uint8, 2> minus6 = { 0x00, 0xFA };
	SIM_EXPECT_EQ(fx.FeatureSet(2, USBAudioMuteSelector, 0, muteOn), ResultStatus::ok);
	SIM_EXPECT_EQ(fx.FeatureSet(2, USBAudioVolumeSelector, 0, minus6), ResultStatus::ok);
	std::array<int16, 48> frame{};
	SIM_EXPECT_EQ(fx.audio.WriteSamples(frame), 48);
	SIM_EXPECT_EQ(fx.audio.GetCounters().microphonePackets, 1U);

	fx.sim.InjectBusReset();
	SIM_EXPECT(!fx.audio.IsStreaming(USBAudioStream::Microphone));
	SIM_EXPECT(!fx.audio.IsStreaming(USBAudioStream::Speaker));
	SIM_EXPECT(fx.sim.FindEndpoint(MicAddress) == nullptr);
	SIM_EXPECT(fx.sim.FindEndpoint(SpkAddress) == nullptr);
	SIM_EXPECT_EQ(fx.audio.GetWritableSamples(), 192U);
	SIM_EXPECT_EQ(fx.audio.GetVolume(USBAudioStream::Microphone), 0);
	SIM_EXPECT(!fx.audio.IsMuted(USBAudioStream::Microphone));
	SIM_EXPECT_EQ(fx.audio.GetCounters().microphonePackets, 1U);

	SIM_EXPECT_EQ(fx.recorder.streams.count, 4U);
	if (fx.recorder.streams.count == 4) {
		SIM_EXPECT(!fx.recorder.streams[2].active);
		SIM_EXPECT(!fx.recorder.streams[3].active);
	}

	// Two events from the host's requests, then the reset: the volume goes back to 0 dB and the mute to off, one event each.
	SIM_EXPECT_EQ(fx.recorder.features.count, 4U);
	if (fx.recorder.features.count == 4) {
		SIM_EXPECT_EQ(fx.recorder.features[3].volume, 0);
		SIM_EXPECT(!fx.recorder.features[3].muted);
	}
}


// SET_CONFIGURATION 0 stops the streams and the core closes their endpoints. The handlers are refused once the device is configured.
void TestDeconfigurationStopsStreams() {
	Basic fx;
	fx.Start();
	SIM_EXPECT_EQ(fx.SetInterface(MicInterface, 1), ResultStatus::ok);
	SIM_EXPECT_EQ(fx.audio.SetStreamHandler(AudioTest::Audio::StreamDelegate()), ResultStatus::busy);

	SIM_EXPECT_EQ(SimHost::Control(fx.sim, SimHost::MakeSetup(0x00, 9, 0, 0, 0)), ResultStatus::ok);
	SIM_EXPECT(!fx.audio.IsStreaming(USBAudioStream::Microphone));
	SIM_EXPECT(fx.sim.FindEndpoint(MicAddress) == nullptr);
	SIM_EXPECT_EQ(fx.recorder.streams.count, 2U);
	if (fx.recorder.streams.count == 2) {
		SIM_EXPECT(!fx.recorder.streams[1].active);
	}
}


// A completion that reaches the function after its stream stopped is ignored: no packet is armed in answer, no sample is copied into the ring,
// and no counter moves. The stream is stopped first, then the completion is handed to the function directly, as a late interrupt would.
void TestLateCompletionAfterStopIsIgnored() {
	Basic fx;
	fx.Start();
	SIM_EXPECT_EQ(fx.SetInterface(MicInterface, 1), ResultStatus::ok);
	SIM_EXPECT_EQ(fx.SetInterface(MicInterface, 0), ResultStatus::ok);

	CountingContext context;
	fx.audio.OnTransferComplete(MicAddress, 96, context);
	SIM_EXPECT_EQ(context.calls, 0U);
	USBAudioCounters counters = fx.audio.GetCounters();
	SIM_EXPECT_EQ(counters.microphonePackets, 1U);
	SIM_EXPECT_EQ(counters.microphoneUnderruns, 1U);
	SIM_EXPECT(fx.sim.FindEndpoint(MicAddress) == nullptr);

	SIM_EXPECT_EQ(fx.SetInterface(SpkInterface, 1), ResultStatus::ok);
	std::array<int16, 96> samples{};
	for (std::size_t index = 0; index < samples.size(); index++) {
		samples[index] = AudioTest::PatternSample(16, index);
	}

	std::array<uint8, 192> packet = AudioTest::SamplesToBytes(samples);
	SIM_EXPECT(fx.sim.IsoOutFrame(SpkAddress, packet) == ResultStatus::ok);
	SIM_EXPECT_EQ(fx.audio.GetReadableSamples(), 96U);
	SIM_EXPECT_EQ(fx.SetInterface(SpkInterface, 0), ResultStatus::ok);
	SIM_EXPECT_EQ(fx.audio.GetReadableSamples(), 0U);

	fx.audio.OnTransferComplete(SpkAddress, 192, context);
	SIM_EXPECT_EQ(context.calls, 0U);
	SIM_EXPECT_EQ(fx.audio.GetReadableSamples(), 0U);
	counters = fx.audio.GetCounters();
	SIM_EXPECT_EQ(counters.speakerPackets, 1U);
	SIM_EXPECT_EQ(counters.speakerOverrunSamples, 0U);
}


// An arm the port refuses is counted, and nothing is armed after it. The completion is handed over with a context that refuses every transfer,
// so the refusal is the only thing that happens: one StartInTransfer, refused, and the empty ring is padded for it.
void TestRefusedMicrophoneArmIsCounted() {
	Basic fx;
	fx.Start();
	SIM_EXPECT_EQ(fx.SetInterface(MicInterface, 1), ResultStatus::ok);

	CountingContext refusing;
	fx.audio.OnTransferComplete(MicAddress, 96, refusing);
	SIM_EXPECT_EQ(refusing.calls, 1U);
	USBAudioCounters counters = fx.audio.GetCounters();
	SIM_EXPECT_EQ(counters.microphonePackets, 1U);
	SIM_EXPECT_EQ(counters.microphoneArmRefusals, 1U);
	SIM_EXPECT_EQ(counters.microphoneUnderruns, 2U);
}


// A microphone arm refused in a completion leaves the stream selected and idle, and the samples taken for the refused packet are dropped. The
// next SET_INTERFACE of alternate 1 restarts the stream: the ring is cleared, the first packet is armed again and the stream runs. The frame
// written after the restart reaches the host within three frames. The first packet armed by a SET_INTERFACE is pending, so its frame is the
// zero-length packet and the silence goes out at the second frame. The frame written after the restart is then sent at the third, and the
// test looks for it over three frames.
void TestRefusedArmThenSetInterfaceRestartsStream() {
	Basic fx;
	fx.Start();
	SIM_EXPECT_EQ(fx.SetInterface(MicInterface, 1), ResultStatus::ok);

	std::array<int16, 48> frameA{};
	std::array<int16, 48> frameB{};
	std::array<int16, 48> frameC{};
	for (std::size_t index = 0; index < 48; index++) {
		frameA[index] = AudioTest::PatternSample(20, index);
		frameB[index] = AudioTest::PatternSample(21, index);
		frameC[index] = AudioTest::PatternSample(22, index);
	}

	SIM_EXPECT_EQ(fx.audio.WriteSamples(frameA), 48);
	CountingContext refusing;
	fx.audio.OnTransferComplete(MicAddress, 96, refusing);
	SIM_EXPECT_EQ(refusing.calls, 1U);
	SIM_EXPECT_EQ(fx.audio.GetCounters().microphoneArmRefusals, 1U);
	SIM_EXPECT_EQ(fx.audio.GetWritableSamples(), 192U);
	SIM_EXPECT(fx.audio.IsStreaming(USBAudioStream::Microphone));
	SIM_EXPECT_EQ(fx.audio.WriteSamples(frameB), 48);

	SIM_EXPECT_EQ(fx.SetInterface(MicInterface, 1), ResultStatus::ok);
	SIM_EXPECT(fx.audio.IsStreaming(USBAudioStream::Microphone));
	SIM_EXPECT(fx.sim.HasArmedTransfer(MicAddress));
	SIM_EXPECT_EQ(fx.audio.GetWritableSamples(), 192U);
	SIM_EXPECT_EQ(fx.audio.GetCounters().microphonePackets, 2U);
	SIM_EXPECT_EQ(fx.recorder.streams.count, 3U);
	if (fx.recorder.streams.count == 3) {
		SIM_EXPECT(!fx.recorder.streams[1].active);
		SIM_EXPECT(fx.recorder.streams[2].active);
	}

	SIM_EXPECT_EQ(fx.audio.WriteSamples(frameC), 48);
	std::array<uint8, 96> bytesC = AudioTest::SamplesToBytes(frameC);
	std::array<uint8, 256> in{};
	uint32 length = 0;
	bool found = false;
	for (std::size_t frameIndex = 0; frameIndex < 3 && !found; frameIndex++) {
		SIM_EXPECT(fx.sim.IsoInFrame(MicAddress, in, length) == ResultStatus::ok);
		found = length == 96U && SimHost::SameBytes(std::span<const uint8>(in).first(96), bytesC);
	}

	SIM_EXPECT(found);
}


// A SET_INTERFACE of alternate 1 whose first arm the port refuses fails the request: the call returns the refusal (the core stalls the request),
// the endpoint it opened is closed again, and no start is reported. The stream stays off, so the next SET_INTERFACE can start it. The
// refusal is driven through OnSetInterface() with a context that refuses every transfer, since the simulator accepts the arm.
void TestRefusedFirstArmFailsSetInterface() {
	Basic fx;
	fx.Start();

	RefusingArmContext refusing;
	SIM_EXPECT_EQ(fx.audio.OnSetInterface(MicInterface, 1, refusing), ResultStatus::notReady);
	SIM_EXPECT_EQ(refusing.opened, 1U);
	SIM_EXPECT_EQ(refusing.closed, 1U);
	SIM_EXPECT(!fx.audio.IsStreaming(USBAudioStream::Microphone));
	SIM_EXPECT_EQ(fx.recorder.streams.count, 0U);
	USBAudioCounters counters = fx.audio.GetCounters();
	SIM_EXPECT_EQ(counters.microphoneArmRefusals, 1U);
	SIM_EXPECT_EQ(counters.microphonePackets, 0U);

	// The speaker's first arm is refused the same way, and counted with the speaker's refusals.
	SIM_EXPECT_EQ(fx.audio.OnSetInterface(SpkInterface, 1, refusing), ResultStatus::notReady);
	SIM_EXPECT_EQ(refusing.opened, 2U);
	SIM_EXPECT_EQ(refusing.closed, 2U);
	SIM_EXPECT(!fx.audio.IsStreaming(USBAudioStream::Speaker));
	SIM_EXPECT_EQ(fx.recorder.streams.count, 0U);
	counters = fx.audio.GetCounters();
	SIM_EXPECT_EQ(counters.speakerArmRefusals, 1U);
	SIM_EXPECT_EQ(counters.microphoneArmRefusals, 1U);
	SIM_EXPECT_EQ(refusing.refusedTransfers, 2U);

	// The next SET_INTERFACE with a port that accepts starts the stream.
	SIM_EXPECT_EQ(fx.SetInterface(MicInterface, 1), ResultStatus::ok);
	SIM_EXPECT(fx.audio.IsStreaming(USBAudioStream::Microphone));
	SIM_EXPECT_EQ(fx.recorder.streams.count, 1U);
}


// A speaker packet that arrives while the next arm is refused is still copied into the ring, and the refusal is counted. The stream stays idle.
void TestRefusedSpeakerArmIsCounted() {
	Basic fx;
	fx.Start();
	SIM_EXPECT_EQ(fx.SetInterface(SpkInterface, 1), ResultStatus::ok);

	CountingContext refusing;
	fx.audio.OnTransferComplete(SpkAddress, 192, refusing);
	SIM_EXPECT_EQ(refusing.calls, 1U);
	SIM_EXPECT_EQ(fx.audio.GetReadableSamples(), 96U);
	USBAudioCounters counters = fx.audio.GetCounters();
	SIM_EXPECT_EQ(counters.speakerPackets, 1U);
	SIM_EXPECT_EQ(counters.speakerArmRefusals, 1U);
	SIM_EXPECT_EQ(counters.speakerPartialPackets, 0U);
}


// A speaker packet that does not hold whole frames is trimmed to them before the ring. The stereo speaker's frame is four bytes. A 3-byte packet
// holds no whole frame, so it gives no samples. A 6-byte packet keeps its first frame, two samples, and drops its last two bytes. Both count as
// partial packets, and the packets after them stay in step with the frames.
void TestSpeakerPartialPacketsAreTrimmed() {
	Basic fx;
	fx.Start();
	SIM_EXPECT_EQ(fx.SetInterface(SpkInterface, 1), ResultStatus::ok);

	const std::array<uint8, 3> threeBytes = { 0x11, 0x22, 0x33 };
	SIM_EXPECT(fx.sim.IsoOutFrame(SpkAddress, threeBytes) == ResultStatus::ok);
	SIM_EXPECT_EQ(fx.audio.GetReadableSamples(), 0U);
	USBAudioCounters counters = fx.audio.GetCounters();
	SIM_EXPECT_EQ(counters.speakerPackets, 1U);
	SIM_EXPECT_EQ(counters.speakerPartialPackets, 1U);
	SIM_EXPECT_EQ(counters.speakerOverrunSamples, 0U);

	const std::array<uint8, 6> sixBytes = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
	SIM_EXPECT(fx.sim.IsoOutFrame(SpkAddress, sixBytes) == ResultStatus::ok);
	SIM_EXPECT_EQ(fx.audio.GetReadableSamples(), 2U);
	counters = fx.audio.GetCounters();
	SIM_EXPECT_EQ(counters.speakerPackets, 2U);
	SIM_EXPECT_EQ(counters.speakerPartialPackets, 2U);
	SIM_EXPECT_EQ(counters.speakerOverrunSamples, 0U);

	// The samples are little endian: 0x01 0x02 is 0x0201 and 0x03 0x04 is 0x0403, and the last two bytes are not among them.
	std::array<int16, 4> out{};
	SIM_EXPECT_EQ(fx.audio.ReadSamples(out), 2U);
	SIM_EXPECT_EQ(out[0], 0x0201);
	SIM_EXPECT_EQ(out[1], 0x0403);
	SIM_EXPECT_EQ(fx.audio.GetReadableSamples(), 0U);
}


// A sampling-frequency SET_CUR to the rate a stream already has changes nothing: the ring keeps its samples, and no rate change is reported.
// The speaker is the same, with a packet in its ring.
void TestRepeatedSamplingRateKeepsRing() {
	MultiRate fx;
	fx.Start();
	SIM_EXPECT_EQ(fx.SetInterface(MicInterface, 1), ResultStatus::ok);

	std::array<int16, 16> frame{};
	for (std::size_t index = 0; index < frame.size(); index++) {
		frame[index] = AudioTest::PatternSample(23, index);
	}

	SIM_EXPECT_EQ(fx.audio.WriteSamples(frame), 16);
	SIM_EXPECT_EQ(fx.audio.GetWritableSamples(), 176U);

	const std::array<uint8, 3> hz16000 = { 0x80, 0x3E, 0x00 };
	SIM_EXPECT_EQ(fx.SamplingSet(MicAddress, hz16000), ResultStatus::ok);
	SIM_EXPECT_EQ(fx.audio.GetRate(USBAudioStream::Microphone), 16000U);
	SIM_EXPECT_EQ(fx.audio.GetWritableSamples(), 176U);
	SIM_EXPECT_EQ(fx.recorder.rates.count, 0U);

	SIM_EXPECT_EQ(fx.SetInterface(SpkInterface, 1), ResultStatus::ok);
	std::array<int16, 96> samples{};
	for (std::size_t index = 0; index < samples.size(); index++) {
		samples[index] = AudioTest::PatternSample(24, index);
	}

	std::array<uint8, 192> packet = AudioTest::SamplesToBytes(samples);
	SIM_EXPECT(fx.sim.IsoOutFrame(SpkAddress, packet) == ResultStatus::ok);
	SIM_EXPECT_EQ(fx.audio.GetReadableSamples(), 96U);

	const std::array<uint8, 3> hz48000 = { 0x80, 0xBB, 0x00 };
	SIM_EXPECT_EQ(fx.SamplingSet(SpkAddress, hz48000), ResultStatus::ok);
	SIM_EXPECT_EQ(fx.audio.GetRate(USBAudioStream::Speaker), 48000U);
	SIM_EXPECT_EQ(fx.audio.GetReadableSamples(), 96U);
	SIM_EXPECT_EQ(fx.recorder.rates.count, 0U);
}


// The rings must hold whole frames. A stereo speaker with 129 samples, and a stereo microphone with 193, are refused at configuration, so the
// device stays unconfigured and the SET_CONFIGURATION is stalled. Rings of whole frames configure.
template<typename TFixture>
void ExpectConfigurationRefused() {
	TFixture fx;
	USBDeviceAdapter::Parameters parameters;
	SIM_EXPECT_EQ(fx.device.Start(parameters), ResultStatus::ok);
	fx.sim.InjectBusReset();
	SIM_EXPECT_EQ(SimHost::Control(fx.sim, SimHost::MakeSetup(0x00, 5, 5, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(SimHost::Control(fx.sim, SimHost::MakeSetup(0x00, 9, 1, 0x0000, 0)), ResultStatus::accessError);
	SIM_EXPECT(fx.device.GetState() != USBDeviceState::Configured);
	SIM_EXPECT(!fx.audio.IsStreaming(USBAudioStream::Microphone));
}


void TestRingNotWholeFramesIsRefused() {
	ExpectConfigurationRefused<OddSpeaker>();
	ExpectConfigurationRefused<OddMic>();

	// The same stereo microphone with a ring of whole frames configures.
	AudioTest::AudioFixture<AudioTest::StereoMicUsb> fx;
	fx.Start();
	SIM_EXPECT_EQ(fx.device.GetState(), USBDeviceState::Configured);
}


// Host frames with no stream open reach no endpoint: the endpoint is not open, so each frame is refused and counts nothing.
void TestNoStreamCountsNothing() {
	Basic fx;
	fx.Start();
	std::array<uint8, 256> in{};
	uint32 length = 0;
	SIM_EXPECT(fx.sim.IsoInFrame(MicAddress, in, length) == ResultStatus::notExist);
	SIM_EXPECT_EQ(length, 0U);

	USBAudioCounters counters = fx.audio.GetCounters();
	SIM_EXPECT_EQ(counters.microphonePackets, 0U);
	SIM_EXPECT_EQ(counters.microphoneUnderruns, 0U);
	SIM_EXPECT_EQ(counters.speakerPackets, 0U);
}


// A stereo 48 kHz microphone and a stereo 48 kHz speaker at once. The device core sets the buffer table from the descriptors (the highest number is
// 2), so the table is 24 bytes. The microphone's IN blocks are 196 bytes each, the speaker's OUT blocks 224, and the pair takes 24 + 128 + 392 + 448
// = 992 of the 1024 bytes. Both SET_INTERFACE requests succeed and both streams run.
void TestStereoPairOpensBothStreams() {
	AudioTest::AudioFixture<AudioTest::StereoPairUsb> fx;
	fx.Start();
	SIM_EXPECT_EQ(fx.SetInterface(MicInterface, 1), ResultStatus::ok);
	const USBDeviceAdapter::EndpointConfig* mic = fx.sim.FindEndpoint(MicAddress);
	SIM_EXPECT(mic != nullptr);
	if (mic != nullptr) {
		SIM_EXPECT_EQ(mic->maxPacketSize, 196);
	}

	SIM_EXPECT_EQ(fx.SetInterface(SpkInterface, 1), ResultStatus::ok);
	const USBDeviceAdapter::EndpointConfig* spk = fx.sim.FindEndpoint(SpkAddress);
	SIM_EXPECT(spk != nullptr);
	if (spk != nullptr) {
		SIM_EXPECT_EQ(spk->maxPacketSize, 196);
	}

	SIM_EXPECT(fx.audio.IsStreaming(USBAudioStream::Microphone));
	SIM_EXPECT(fx.audio.IsStreaming(USBAudioStream::Speaker));
	SIM_EXPECT(fx.sim.HasArmedTransfer(MicAddress));
	SIM_EXPECT(fx.sim.HasArmedTransfer(SpkAddress));
	SIM_EXPECT_EQ(fx.recorder.streams.count, 2U);
}


// The other order: the speaker is opened first, then the microphone. The pair fits in either order, and the endpoint opened first keeps its
// packet armed while the second one opens.
void TestStereoPairSpeakerFirstOpensBothStreams() {
	AudioTest::AudioFixture<AudioTest::StereoPairUsb> fx;
	fx.Start();
	SIM_EXPECT_EQ(fx.SetInterface(SpkInterface, 1), ResultStatus::ok);
	SIM_EXPECT(fx.sim.FindEndpoint(SpkAddress) != nullptr);
	SIM_EXPECT(fx.sim.HasArmedTransfer(SpkAddress));

	SIM_EXPECT_EQ(fx.SetInterface(MicInterface, 1), ResultStatus::ok);
	SIM_EXPECT(fx.sim.FindEndpoint(MicAddress) != nullptr);
	SIM_EXPECT(fx.audio.IsStreaming(USBAudioStream::Microphone));
	SIM_EXPECT(fx.audio.IsStreaming(USBAudioStream::Speaker));
	SIM_EXPECT(fx.sim.HasArmedTransfer(SpkAddress));
	SIM_EXPECT(fx.sim.HasArmedTransfer(MicAddress));
	SIM_EXPECT_EQ(fx.recorder.streams.count, 2U);
}


// The device core passes the highest endpoint number of its descriptors (2, from 0x81 and 0x02), so the table is 24 bytes and endpoint 0 ends at
// 152. A 436-byte isochronous IN endpoint then fits in the simulator, which the 64-byte table would refuse.
void TestStereoPairTableFollowsDescriptor() {
	AudioTest::AudioFixture<AudioTest::StereoPairUsb> fx;
	fx.Start();
	USBDeviceAdapter::EndpointConfig wide;
	wide.address = MicAddress;
	wide.type = USBEndpointType::Isochronous;
	wide.maxPacketSize = 436;
	SIM_EXPECT_EQ(fx.sim.OpenEndpoint(wide), ResultStatus::ok);
}


int main() {
	TestAlternatesOpenAndCloseEndpoints();
	TestControlInterfaceAcceptsAlternateZeroOnly();
	TestMicrophoneSendsOnePacketPerFrame();
	TestMicrophoneUnderrunIsSilence();
	TestArmedPacketWaitsForTheHost();
	TestStoppingEmptiesRingAndRefusesSamples();
	TestMicrophoneRestartAfterStop();
	TestSpeakerReceivesPackets();
	TestSpeakerOverflowDropsNewestSamples();
	TestRateChangeChangesPacketSize();
	TestSamplingChangeClearsStreamingRing();
	TestBusResetStopsStreamsAndRestoresDefaults();
	TestDeconfigurationStopsStreams();
	TestLateCompletionAfterStopIsIgnored();
	TestRefusedMicrophoneArmIsCounted();
	TestRefusedArmThenSetInterfaceRestartsStream();
	TestRefusedFirstArmFailsSetInterface();
	TestRefusedSpeakerArmIsCounted();
	TestSpeakerPartialPacketsAreTrimmed();
	TestRepeatedSamplingRateKeepsRing();
	TestRingNotWholeFramesIsRefused();
	TestNoStreamCountsNothing();
	TestStereoPairOpensBothStreams();
	TestStereoPairSpeakerFirstOpensBothStreams();
	TestStereoPairTableFollowsDescriptor();
	return SimTest::Finish("TestAudioStreaming");
}
