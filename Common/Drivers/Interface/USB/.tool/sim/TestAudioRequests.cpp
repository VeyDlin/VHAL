#include <VHAL.h>
#include "AudioTestDevices.h"
#include "SimAssert.h"
#include "SimHost.h"
#include <Drivers/Interface/USB/Class/Audio/USBAudioBuilder.h>
#include <array>
#include <cstddef>
#include <span>


// The class requests of the audio function through the simulated host: the feature units (MUTE and VOLUME, with GET_CUR, SET_CUR and the
// volume's GET_MIN, GET_MAX and GET_RES) and the sampling frequency of the endpoints. The expected bytes are written from the audio class:
// volume values are 1/256 dB, little endian, -60 dB is -15360 (00 C4) and 0 dB is 0. Every request that is not served ends with a stall.


namespace {


using Basic = AudioTest::AudioFixture<AudioTest::BasicUsb>;
using MultiRate = AudioTest::AudioFixture<AudioTest::MultiRateUsb>;


constexpr uint8 MicUnit = 2;
constexpr uint8 SpkUnit = 5;
constexpr uint8 MicAddress = 0x81;
constexpr uint8 SpkAddress = 0x02;


}


// Mute of the microphone: GET_CUR answers 0, SET_CUR 1 is served and reported, a value other than 0 or 1 is stalled, and the mute has no
// range, so GET_MIN is stalled. A SET_CUR with a data stage of the wrong length is stalled too.
void TestMuteGetSetAndStall() {
	Basic fx;
	fx.Start();

	std::array<uint8, 1> out{};
	uint32 received = 0;
	SIM_EXPECT_EQ(fx.FeatureGet(MicUnit, USBAudioMuteSelector, 0, USBAudioGetCur, out, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 1U);
	SIM_EXPECT_EQ(out[0], 0);

	const std::array<uint8, 1> muteOn = { 1 };
	SIM_EXPECT_EQ(fx.FeatureSet(MicUnit, USBAudioMuteSelector, 0, muteOn), ResultStatus::ok);
	SIM_EXPECT_EQ(fx.FeatureGet(MicUnit, USBAudioMuteSelector, 0, USBAudioGetCur, out, received), ResultStatus::ok);
	SIM_EXPECT_EQ(out[0], 1);
	SIM_EXPECT(fx.audio.IsMuted(USBAudioStream::Microphone));
	SIM_EXPECT_EQ(fx.recorder.features.count, 1U);
	if (fx.recorder.features.count == 1) {
		SIM_EXPECT(fx.recorder.features[0].stream == USBAudioStream::Microphone);
		SIM_EXPECT(fx.recorder.features[0].muted);
		SIM_EXPECT_EQ(fx.recorder.features[0].volume, 0);
	}

	const std::array<uint8, 1> notBoolean = { 2 };
	SIM_EXPECT_EQ(fx.FeatureSet(MicUnit, USBAudioMuteSelector, 0, notBoolean), ResultStatus::accessError);
	SIM_EXPECT(fx.audio.IsMuted(USBAudioStream::Microphone));
	SIM_EXPECT_EQ(fx.recorder.features.count, 1U);

	SIM_EXPECT_EQ(fx.FeatureGet(MicUnit, USBAudioMuteSelector, 0, USBAudioGetMin, out, received), ResultStatus::accessError);

	const std::array<uint8, 2> twoBytes = { 0, 0 };
	SIM_EXPECT_EQ(fx.FeatureSet(MicUnit, USBAudioMuteSelector, 0, twoBytes), ResultStatus::accessError);
	SIM_EXPECT_EQ(fx.recorder.features.count, 1U);
	SIM_EXPECT_EQ(fx.device.GetState(), USBDeviceState::Configured);
}


// Volume of the microphone in 1/256 dB. -6 dB (00 FA) is served. The range is -60 dB (00 C4) to 0 dB (00 00): -61 dB (00 C3) and +1 dB
// (00 01) are stalled, and a SET_CUR with one byte is stalled. GET_MIN, GET_MAX and GET_RES answer -60 dB, 0 dB and 1 dB.
void TestVolumeRangeAndStalls() {
	Basic fx;
	fx.Start();

	std::array<uint8, 2> out{};
	uint32 received = 0;
	const std::array<uint8, 2> minus6 = { 0x00, 0xFA };
	SIM_EXPECT_EQ(fx.FeatureSet(MicUnit, USBAudioVolumeSelector, 0, minus6), ResultStatus::ok);
	SIM_EXPECT_EQ(fx.FeatureGet(MicUnit, USBAudioVolumeSelector, 0, USBAudioGetCur, out, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 2U);
	SIM_EXPECT_EQ(out[0], 0x00);
	SIM_EXPECT_EQ(out[1], 0xFA);
	SIM_EXPECT_EQ(fx.audio.GetVolume(USBAudioStream::Microphone), -1536);
	SIM_EXPECT_EQ(fx.recorder.features.count, 1U);
	if (fx.recorder.features.count == 1) {
		SIM_EXPECT_EQ(fx.recorder.features[0].volume, -1536);
		SIM_EXPECT(!fx.recorder.features[0].muted);
	}

	const std::array<uint8, 2> minus60 = { 0x00, 0xC4 };
	SIM_EXPECT_EQ(fx.FeatureSet(MicUnit, USBAudioVolumeSelector, 0, minus60), ResultStatus::ok);
	SIM_EXPECT_EQ(fx.audio.GetVolume(USBAudioStream::Microphone), -15360);

	const std::array<uint8, 2> minus61 = { 0x00, 0xC3 };
	SIM_EXPECT_EQ(fx.FeatureSet(MicUnit, USBAudioVolumeSelector, 0, minus61), ResultStatus::accessError);
	const std::array<uint8, 2> plus1 = { 0x00, 0x01 };
	SIM_EXPECT_EQ(fx.FeatureSet(MicUnit, USBAudioVolumeSelector, 0, plus1), ResultStatus::accessError);
	SIM_EXPECT_EQ(fx.audio.GetVolume(USBAudioStream::Microphone), -15360);

	const std::array<uint8, 1> oneByte = { 0x00 };
	SIM_EXPECT_EQ(fx.FeatureSet(MicUnit, USBAudioVolumeSelector, 0, oneByte), ResultStatus::accessError);

	SIM_EXPECT_EQ(fx.FeatureGet(MicUnit, USBAudioVolumeSelector, 0, USBAudioGetMin, out, received), ResultStatus::ok);
	SIM_EXPECT_EQ(out[0], 0x00);
	SIM_EXPECT_EQ(out[1], 0xC4);
	SIM_EXPECT_EQ(fx.FeatureGet(MicUnit, USBAudioVolumeSelector, 0, USBAudioGetMax, out, received), ResultStatus::ok);
	SIM_EXPECT_EQ(out[0], 0x00);
	SIM_EXPECT_EQ(out[1], 0x00);
	SIM_EXPECT_EQ(fx.FeatureGet(MicUnit, USBAudioVolumeSelector, 0, USBAudioGetRes, out, received), ResultStatus::ok);
	SIM_EXPECT_EQ(out[0], 0x00);
	SIM_EXPECT_EQ(out[1], 0x01);
}


// Requests that name something the function does not have are stalled: an unknown selector, a channel other than the master, a unit
// number that is no feature unit, an unknown bRequest, and a request to the speaker's interface. The speaker's controls work on their own.
void TestUnknownControlsAreStalled() {
	Basic fx;
	fx.Start();

	std::array<uint8, 2> out{};
	uint32 received = 0;
	const std::array<uint8, 1> oneByte = { 1 };
	SIM_EXPECT_EQ(fx.FeatureGet(MicUnit, 0x03, 0, USBAudioGetCur, out, received), ResultStatus::accessError);
	SIM_EXPECT_EQ(fx.FeatureSet(MicUnit, USBAudioMuteSelector, 1, oneByte), ResultStatus::accessError);
	SIM_EXPECT_EQ(fx.FeatureGet(9, USBAudioMuteSelector, 0, USBAudioGetCur, out, received), ResultStatus::accessError);
	SIM_EXPECT_EQ(fx.FeatureGet(MicUnit, USBAudioMuteSelector, 0, 0x85, out, received), ResultStatus::accessError);

	// The speaker's unit answers on its own: -3 dB on the speaker does not change the microphone.
	const std::array<uint8, 2> minus3 = { 0x00, 0xFD };
	SIM_EXPECT_EQ(fx.FeatureSet(SpkUnit, USBAudioVolumeSelector, 0, minus3), ResultStatus::ok);
	SIM_EXPECT_EQ(fx.audio.GetVolume(USBAudioStream::Speaker), -768);
	SIM_EXPECT_EQ(fx.audio.GetVolume(USBAudioStream::Microphone), 0);
	const std::array<uint8, 1> muteOn = { 1 };
	SIM_EXPECT_EQ(fx.FeatureSet(SpkUnit, USBAudioMuteSelector, 0, muteOn), ResultStatus::ok);
	SIM_EXPECT(fx.audio.IsMuted(USBAudioStream::Speaker));
	SIM_EXPECT(!fx.audio.IsMuted(USBAudioStream::Microphone));
	SIM_EXPECT_EQ(fx.recorder.features.count, 2U);
	if (fx.recorder.features.count == 2) {
		SIM_EXPECT(fx.recorder.features[1].stream == USBAudioStream::Speaker);
	}

	// The interface request to the speaker's AudioStreaming interface (2) is not a control of the function.
	SIM_EXPECT_EQ(fx.ClassRequest(0xA1, USBAudioGetCur, 0x0100, 0x0002, std::span<const uint8>(), out, received), ResultStatus::accessError);
}


// The basic device has one sampling rate per stream, so it has no sampling frequency control: GET_CUR on the endpoint is stalled.
void TestSingleRateHasNoSamplingControl() {
	Basic fx;
	fx.Start();

	std::array<uint8, 3> rate{};
	uint32 received = 0;
	SIM_EXPECT_EQ(fx.SamplingGet(MicAddress, rate, received), ResultStatus::accessError);
	SIM_EXPECT_EQ(fx.SamplingGet(SpkAddress, rate, received), ResultStatus::accessError);
}


// The multi-rate device: GET_CUR answers the first rate listed (16 kHz for the microphone, 48 kHz for the speaker), SET_CUR to a listed
// rate is served and reported, and SET_CUR to a rate not listed (44100 Hz, or a listed rate of the other stream) is stalled. The mute is
// not a control of the multi-rate microphone, so its requests are stalled.
void TestMultiRateSamplingFrequency() {
	MultiRate fx;
	fx.Start();

	std::array<uint8, 3> rate{};
	uint32 received = 0;
	SIM_EXPECT_EQ(fx.SamplingGet(MicAddress, rate, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 3U);
	SIM_EXPECT_EQ(rate[0], 0x80);
	SIM_EXPECT_EQ(rate[1], 0x3E);
	SIM_EXPECT_EQ(rate[2], 0x00);

	SIM_EXPECT_EQ(fx.SamplingGet(SpkAddress, rate, received), ResultStatus::ok);
	SIM_EXPECT_EQ(rate[0], 0x80);
	SIM_EXPECT_EQ(rate[1], 0xBB);
	SIM_EXPECT_EQ(rate[2], 0x00);

	const std::array<uint8, 3> hz48000 = { 0x80, 0xBB, 0x00 };
	SIM_EXPECT_EQ(fx.SamplingSet(MicAddress, hz48000), ResultStatus::ok);
	SIM_EXPECT_EQ(fx.SamplingGet(MicAddress, rate, received), ResultStatus::ok);
	SIM_EXPECT_EQ(rate[1], 0xBB);
	SIM_EXPECT_EQ(fx.recorder.rates.count, 1U);
	if (fx.recorder.rates.count == 1) {
		SIM_EXPECT(fx.recorder.rates[0].stream == USBAudioStream::Microphone);
		SIM_EXPECT_EQ(fx.recorder.rates[0].rate, 48000U);
	}

	// 44100 Hz is not listed: 44100 is 0x00AC44, which is 44 AC 00.
	const std::array<uint8, 3> hz44100 = { 0x44, 0xAC, 0x00 };
	SIM_EXPECT_EQ(fx.SamplingSet(MicAddress, hz44100), ResultStatus::accessError);
	SIM_EXPECT_EQ(fx.recorder.rates.count, 1U);

	// 32000 is listed by the microphone but not by the speaker.
	const std::array<uint8, 3> hz32000 = { 0x00, 0x7D, 0x00 };
	SIM_EXPECT_EQ(fx.SamplingSet(SpkAddress, hz32000), ResultStatus::accessError);
	SIM_EXPECT_EQ(fx.SamplingSet(MicAddress, hz32000), ResultStatus::ok);
	SIM_EXPECT_EQ(fx.recorder.rates.count, 2U);

	// An endpoint that is not a stream is stalled.
	SIM_EXPECT_EQ(fx.SamplingGet(0x83, rate, received), ResultStatus::accessError);

	std::array<uint8, 1> mute{};
	SIM_EXPECT_EQ(fx.FeatureGet(MicUnit, USBAudioMuteSelector, 0, USBAudioGetCur, mute, received), ResultStatus::accessError);
	SIM_EXPECT_EQ(fx.device.GetState(), USBDeviceState::Configured);
}


// A request the function does not serve gets a stall, and the device stays configured: the AC interface's unknown request.
void TestUnknownRequestOnControlInterfaceIsStalled() {
	Basic fx;
	fx.Start();

	std::array<uint8, 2> out{};
	uint32 received = 0;
	SIM_EXPECT_EQ(fx.ClassRequest(0xA1, 0x85, 0x0200, 0x0000, std::span<const uint8>(), out, received), ResultStatus::accessError);
	SIM_EXPECT_EQ(fx.device.GetState(), USBDeviceState::Configured);
}


// A GET with a wLength shorter than its value answers the first wLength bytes, and a longer one answers the whole value, so its data stage
// ends short. The volume is set to -1/256 dB (0xFFFF): its low byte is 0xFF, so the first byte shows which bytes went out. A wLength of 0 has
// no data stage and is stalled. The mute's value is one byte, so a longer request gets that byte.
void TestGetLengthRules() {
	Basic fx;
	fx.Start();

	const std::array<uint8, 2> minusOneUnit = { 0xFF, 0xFF };
	SIM_EXPECT_EQ(fx.FeatureSet(MicUnit, USBAudioVolumeSelector, 0, minusOneUnit), ResultStatus::ok);
	SIM_EXPECT_EQ(fx.audio.GetVolume(USBAudioStream::Microphone), -1);

	uint32 received = 0;
	std::array<uint8, 3> out{};
	SIM_EXPECT_EQ(fx.FeatureGet(MicUnit, USBAudioVolumeSelector, 0, USBAudioGetCur, std::span<uint8>(out).first(1), received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 1U);
	SIM_EXPECT_EQ(out[0], 0xFF);

	out = {};
	SIM_EXPECT_EQ(fx.FeatureGet(MicUnit, USBAudioVolumeSelector, 0, USBAudioGetCur, out, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 2U);
	SIM_EXPECT_EQ(out[0], 0xFF);
	SIM_EXPECT_EQ(out[1], 0xFF);
	SIM_EXPECT_EQ(out[2], 0);

	SIM_EXPECT_EQ(fx.FeatureGet(MicUnit, USBAudioVolumeSelector, 0, USBAudioGetCur, std::span<uint8>(), received), ResultStatus::accessError);
	SIM_EXPECT_EQ(fx.device.GetState(), USBDeviceState::Configured);

	const std::array<uint8, 1> muteOn = { 1 };
	SIM_EXPECT_EQ(fx.FeatureSet(MicUnit, USBAudioMuteSelector, 0, muteOn), ResultStatus::ok);
	std::array<uint8, 2> two{};
	SIM_EXPECT_EQ(fx.FeatureGet(MicUnit, USBAudioMuteSelector, 0, USBAudioGetCur, two, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 1U);
	SIM_EXPECT_EQ(two[0], 1);
	SIM_EXPECT_EQ(fx.FeatureGet(MicUnit, USBAudioMuteSelector, 0, USBAudioGetCur, std::span<uint8>(), received), ResultStatus::accessError);
}


// The range GETs follow the same rules: a one-byte GET_RES gets the low byte of 1 dB (0x0100), and a three-byte GET_MIN gets the whole
// -60 dB value (0xC400) and stops short. A zero wLength is stalled.
void TestRangeGetLengthRules() {
	Basic fx;
	fx.Start();

	uint32 received = 0;
	std::array<uint8, 1> one{};
	SIM_EXPECT_EQ(fx.FeatureGet(MicUnit, USBAudioVolumeSelector, 0, USBAudioGetRes, one, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 1U);
	SIM_EXPECT_EQ(one[0], 0x00);

	std::array<uint8, 3> three{};
	SIM_EXPECT_EQ(fx.FeatureGet(MicUnit, USBAudioVolumeSelector, 0, USBAudioGetMin, three, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 2U);
	SIM_EXPECT_EQ(three[0], 0x00);
	SIM_EXPECT_EQ(three[1], 0xC4);

	SIM_EXPECT_EQ(fx.FeatureGet(MicUnit, USBAudioVolumeSelector, 0, USBAudioGetMax, std::span<uint8>(), received), ResultStatus::accessError);
	SIM_EXPECT_EQ(fx.device.GetState(), USBDeviceState::Configured);
}


// The sampling frequency GET follows the same rules on the multi-rate microphone (16 kHz, 80 3E 00). A short wLength gets the leading
// bytes, a longer one the three bytes of the rate, and a zero wLength is stalled. The SET keeps its exact length: two bytes are stalled.
void TestSamplingLengthRules() {
	MultiRate fx;
	fx.Start();

	uint32 received = 0;
	std::array<uint8, 2> two{};
	SIM_EXPECT_EQ(fx.SamplingGet(MicAddress, two, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 2U);
	SIM_EXPECT_EQ(two[0], 0x80);
	SIM_EXPECT_EQ(two[1], 0x3E);

	std::array<uint8, 4> four{};
	SIM_EXPECT_EQ(fx.SamplingGet(MicAddress, four, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 3U);
	SIM_EXPECT_EQ(four[0], 0x80);
	SIM_EXPECT_EQ(four[1], 0x3E);
	SIM_EXPECT_EQ(four[2], 0x00);
	SIM_EXPECT_EQ(four[3], 0);

	SIM_EXPECT_EQ(fx.SamplingGet(MicAddress, std::span<uint8>(), received), ResultStatus::accessError);

	const std::array<uint8, 2> twoBytes = { 0x80, 0x3E };
	SIM_EXPECT_EQ(fx.SamplingSet(MicAddress, twoBytes), ResultStatus::accessError);
	SIM_EXPECT_EQ(fx.recorder.rates.count, 0U);
	SIM_EXPECT_EQ(fx.device.GetState(), USBDeviceState::Configured);
}


int main() {
	TestMuteGetSetAndStall();
	TestVolumeRangeAndStalls();
	TestUnknownControlsAreStalled();
	TestSingleRateHasNoSamplingControl();
	TestMultiRateSamplingFrequency();
	TestUnknownRequestOnControlInterfaceIsStalled();
	TestGetLengthRules();
	TestRangeGetLengthRules();
	TestSamplingLengthRules();
	return SimTest::Finish("TestAudioRequests");
}
