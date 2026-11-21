#include <VHAL.h>
#include "SimAssert.h"
#include <DemoProducts.h>
#include <DemoUSBAudio/Audio/AudioSignal.h>
#include <cmath>
#include <cstddef>


// The pure signal code of the audio demo: the 1 kHz tone table, the volume gains, the volume scaling and the loopback downmix. The
// expected values are written here from the arithmetic, not taken from the code's output. The sine table is checked against std::sin in
// double precision, with the rounding of one LSB allowed. The gains are 32768 times 10^(-dB / 20), rounded to the nearest integer.
namespace {


constexpr double TestPi = 3.14159265358979323846;


// The demo's constants, which the PC tool and the device both use. A change to one of them is a change of the product.
static_assert(DemoProducts::AudioProductId == 0x0006, "the audio demo's PID is 0006");
static_assert(DemoProducts::AudioSampleRate == 48000, "the audio demo runs at 48 kHz");
static_assert(DemoProducts::AudioToneFrequency == 1000, "the tone is 1 kHz");
static_assert(DemoProducts::AudioToneAmplitude == 8000, "the tone's peak is 8000 LSB");
static_assert(DemoAudio::ToneSamplesPerPeriod == 48, "one period of 1 kHz at 48 kHz is 48 samples");


// Quarter and eighth points of the period, worked out by hand. The angle of index n is 2 pi n / 48, so index 6 is pi / 4, and
// 8000 sin(pi / 4) = 5656.85 rounds to 5657.
void TestToneHandValues() {
	SIM_EXPECT_EQ(DemoAudio::ToneTable[0], 0);
	SIM_EXPECT_EQ(DemoAudio::ToneTable[6], 5657);
	SIM_EXPECT_EQ(DemoAudio::ToneTable[12], 8000);
	SIM_EXPECT_EQ(DemoAudio::ToneTable[18], 5657);
	SIM_EXPECT_EQ(DemoAudio::ToneTable[24], 0);
	SIM_EXPECT_EQ(DemoAudio::ToneTable[30], -5657);
	SIM_EXPECT_EQ(DemoAudio::ToneTable[36], -8000);
	SIM_EXPECT_EQ(DemoAudio::ToneTable[42], -5657);
}


void TestToneMatchesSine() {
	bool within = true;
	for (std::size_t index = 0; index < DemoAudio::ToneSamplesPerPeriod; index++) {
		double angle = 2.0 * TestPi * static_cast<double>(index) / 48.0;
		double exact = 8000.0 * std::sin(angle);
		double difference = static_cast<double>(DemoAudio::ToneTable[index]) - exact;
		if (std::fabs(difference) > 0.5 + 1e-9) {
			within = false;
		}
	}

	SIM_EXPECT(within);
}


void TestToneWrapsEachPeriod() {
	SIM_EXPECT_EQ(DemoAudio::ToneSample(0), 0);
	SIM_EXPECT_EQ(DemoAudio::ToneSample(48), 0);
	SIM_EXPECT_EQ(DemoAudio::ToneSample(60), 8000);
	SIM_EXPECT_EQ(DemoAudio::ToneSample(12 + 48 * 1000), 8000);
	SIM_EXPECT_EQ(DemoAudio::ToneSample(36 + 48 * 7), -8000);
}


// Gains in Q15 at whole decibels: 0 dB is 32768, -6 dB is 16422.99 (rounds to 16423), -20 dB is 3276.8 (3277), -40 dB is 327.68 (328),
// -60 dB is 32.77 (33).
void TestVolumeGains() {
	SIM_EXPECT_EQ(DemoAudio::VolumeGains[0], 32768);
	SIM_EXPECT_EQ(DemoAudio::VolumeGains[6], 16423);
	SIM_EXPECT_EQ(DemoAudio::VolumeGains[20], 3277);
	SIM_EXPECT_EQ(DemoAudio::VolumeGains[40], 328);
	SIM_EXPECT_EQ(DemoAudio::VolumeGains[60], 33);
}


// A volume is in 1/256 dB and rounds to the nearest whole decibel. -5.5 dB rounds up to 6 dB.
void TestVolumeIndex() {
	SIM_EXPECT_EQ(DemoAudio::VolumeIndex(0), 0U);
	SIM_EXPECT_EQ(DemoAudio::VolumeIndex(-1280), 5U);
	SIM_EXPECT_EQ(DemoAudio::VolumeIndex(-1407), 5U);
	SIM_EXPECT_EQ(DemoAudio::VolumeIndex(-1408), 6U);
	SIM_EXPECT_EQ(DemoAudio::VolumeIndex(-15360), 60U);
	SIM_EXPECT_EQ(DemoAudio::VolumeIndex(-20000), 60U);
	SIM_EXPECT_EQ(DemoAudio::VolumeIndex(100), 0U);
}


void TestScaleSample() {
	SIM_EXPECT_EQ(DemoAudio::ScaleSample(10000, 0, false), 10000);
	SIM_EXPECT_EQ(DemoAudio::ScaleSample(10000, 0, true), 0);
	SIM_EXPECT_EQ(DemoAudio::ScaleSample(-10000, 0, true), 0);
	// -20 dB is a tenth: 10000 gives 1000, and -10000 gives -1000.
	SIM_EXPECT_EQ(DemoAudio::ScaleSample(10000, -5120, false), 1000);
	SIM_EXPECT_EQ(DemoAudio::ScaleSample(-10000, -5120, false), -1000);
	// -6 dB of 10000 is 5011.87, which rounds to 5012.
	SIM_EXPECT_EQ(DemoAudio::ScaleSample(10000, -1536, false), 5012);
	// -60 dB of 30000 is 30. A volume below -60 dB is taken as -60 dB.
	SIM_EXPECT_EQ(DemoAudio::ScaleSample(30000, -15360, false), 30);
	SIM_EXPECT_EQ(DemoAudio::ScaleSample(30000, -20000, false), 30);
	// Full scale stays in range at unity gain.
	SIM_EXPECT_EQ(DemoAudio::ScaleSample(32767, 0, false), 32767);
	SIM_EXPECT_EQ(DemoAudio::ScaleSample(-32768, 0, false), -32768);
}


// The mean of two channels, rounded down (-3 and 0 give -1.5, which is -2 rounded down).
void TestDownmixFrame() {
	SIM_EXPECT_EQ(DemoAudio::DownmixFrame(1000, 1001), 1000);
	SIM_EXPECT_EQ(DemoAudio::DownmixFrame(-3, 0), -2);
	SIM_EXPECT_EQ(DemoAudio::DownmixFrame(-1, 0), -1);
	SIM_EXPECT_EQ(DemoAudio::DownmixFrame(32767, 32767), 32767);
	SIM_EXPECT_EQ(DemoAudio::DownmixFrame(-32768, -32768), -32768);
}


// A mono signal sent on both channels comes back unchanged, which is what the loopback test on the PC relies on.
void TestDownmixOfEqualChannelsIsExact() {
	const int16 values[] = { -32768, -12345, -1, 0, 1, 777, 32767 };
	bool exact = true;
	for (int16 value : values) {
		if (DemoAudio::DownmixFrame(value, value) != value) {
			exact = false;
		}
	}

	SIM_EXPECT(exact);
}


// The tone phase moves by the samples written and wraps at the period: 0 plus 16 is 16, 40 plus 16 is 56 and wraps to 8, and 48 frames
// is one whole period, so the phase returns to 0.
void TestNextTonePhase() {
	SIM_EXPECT_EQ(DemoAudio::NextTonePhase(0U, 16U), 16U);
	SIM_EXPECT_EQ(DemoAudio::NextTonePhase(40U, 16U), 8U);
	SIM_EXPECT_EQ(DemoAudio::NextTonePhase(47U, 1U), 0U);
	SIM_EXPECT_EQ(DemoAudio::NextTonePhase(0U, 48U), 0U);
	SIM_EXPECT_EQ(DemoAudio::NextTonePhase(0U, 192U), 0U);
}


// A sample is signal when its magnitude is above 64 LSB. Windows dither of -1, 0 and +1 is silence, and so is a chunk of zeros and an
// empty chunk. 64 and -64 are silence, and 65 and -65 are sound.
void TestHasSignal() {
	SIM_EXPECT_EQ(DemoAudio::SignalThreshold, 64);
	const int16 silence[4] = { 0, 0, 0, 0 };
	const int16 dither[6] = { -1, 0, 1, -1, 1, 0 };
	const int16 edgeLow[3] = { 64, -64, 0 };
	const int16 justAbovePositive[3] = { 0, 65, 0 };
	const int16 justAboveNegative[3] = { 0, 0, -65 };
	const int16 loopbackTone[2] = { 0, 8000 };
	SIM_EXPECT(!DemoAudio::HasSignal(std::span<const int16>(silence)));
	SIM_EXPECT(!DemoAudio::HasSignal(std::span<const int16>(dither)));
	SIM_EXPECT(!DemoAudio::HasSignal(std::span<const int16>(edgeLow)));
	SIM_EXPECT(DemoAudio::HasSignal(std::span<const int16>(justAbovePositive)));
	SIM_EXPECT(DemoAudio::HasSignal(std::span<const int16>(justAboveNegative)));
	SIM_EXPECT(DemoAudio::HasSignal(std::span<const int16>(loopbackTone)));
	SIM_EXPECT(!DemoAudio::HasSignal(std::span<const int16>()));
}


// The backlog the pump drops: the ring holds 384 and keeps 192 (two packets of 96). 288 is 96 above 192, and a ring at or below 192 keeps
// all of it.
void TestSpeakerExcess() {
	SIM_EXPECT_EQ(DemoAudio::SpeakerExcess(384U, 192U), 192U);
	SIM_EXPECT_EQ(DemoAudio::SpeakerExcess(288U, 192U), 96U);
	SIM_EXPECT_EQ(DemoAudio::SpeakerExcess(192U, 192U), 0U);
	SIM_EXPECT_EQ(DemoAudio::SpeakerExcess(96U, 192U), 0U);
	SIM_EXPECT_EQ(DemoAudio::SpeakerExcess(0U, 192U), 0U);
}


}


int main() {
	TestToneHandValues();
	TestToneMatchesSine();
	TestToneWrapsEachPeriod();
	TestVolumeGains();
	TestVolumeIndex();
	TestScaleSample();
	TestDownmixFrame();
	TestDownmixOfEqualChannelsIsExact();
	TestNextTonePhase();
	TestHasSignal();
	TestSpeakerExcess();
	return SimTest::Finish("TestAudioDemo");
}
