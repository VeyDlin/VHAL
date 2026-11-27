#include <Utilities/Hardware/Tap/TapDetector.h>
#include <VhalCheck.h>
#include <cmath>
#include <vector>


// Host tests of TapDetector: knocks on synthetic 1 kHz traces, the classification of the faces, the acceptance checks in their
// order, the single and double taps, and the thresholds.


namespace {


constexpr float pi = 3.14159265358979f;


// Taps: 1 kHz samples on a gravity baseline with a small noise. A knock is an asymmetric impulse (rising quickly, then
// decaying slowly), so the onset is the largest jerk. The optional ringing decays after the knock.
class TapRig {
public:
	TapDetector detector;
	uint64 timeUs = 0;
	float noiseG = 0.002f;


private:
	uint32 seed = 99;


public:
	void Step(float x, float y, float z) {
		timeUs = timeUs + 1000;
		TapDetector::Sample sample;
		sample.timestampUs = timeUs;
		sample.accelX = x + Noise();
		sample.accelY = y + Noise();
		sample.accelZ = z + Noise();
		detector.Update(sample);
	}


	void Idle(float seconds) {
		uint32 steps = static_cast<uint32>(seconds * 1000.0f + 0.5f);
		for (uint32 step = 0; step < steps; step++) {
			Step(0.0f, 0.0f, 1.0f);
		}
	}


	// Knock on the face of the given axis. sign -1 gives the negative face.
	void Knock(uint32 axis, float sign, float amplitudeG, bool ringing) {
		const float tauSec = 0.003f;
		uint32 steps = 15;
		for (uint32 step = 1; step <= steps; step++) {
			float t = static_cast<float>(step) * 0.001f;
			float value = amplitudeG * (t / tauSec) * std::exp(-t / tauSec);
			Emit(axis, sign * value);
		}

		if (ringing) {
			for (uint32 step = 1; step <= 40; step++) {
				float t = static_cast<float>(step) * 0.001f;
				float value = 0.04f * std::exp(-t / 0.01f) * std::sin(2.0f * pi * 300.0f * t);
				Emit(axis, value);
			}
		}
	}


	void Shake(float seconds, float frequencyHz, float amplitudeG) {
		uint32 steps = static_cast<uint32>(seconds * 1000.0f + 0.5f);
		for (uint32 step = 0; step < steps; step++) {
			float t = static_cast<float>(step) * 0.001f;
			Step(amplitudeG * std::sin(2.0f * pi * frequencyHz * t), 0.0f, 1.0f);
		}
	}


	// Ringing of the case after an impact on the given axis: 40 ms of 300 Hz decaying with 10 ms.
	void Ring(uint32 axis, float amplitudeG) {
		for (uint32 step = 1; step <= 40; step++) {
			float t = static_cast<float>(step) * 0.001f;
			float value = amplitudeG * std::exp(-t / 0.01f) * std::sin(2.0f * pi * 300.0f * t);
			Emit(axis, value);
		}
	}


	// Hand motion around the rest pose: a 2 Hz and an 8 Hz sway of 0.3 g each on X and Y. The envelope sin^2 starts and ends
	// the motion at rest, so the sway itself makes no step. The knock adds its impulse (on X, sign +-1) at knockAtSec.
	void SwayWithKnock(float seconds, float knockAtSec, float knockAmplitudeG, float knockSign) {
		uint32 steps = static_cast<uint32>(seconds * 1000.0f + 0.5f);
		const float tauSec = 0.003f;
		for (uint32 step = 0; step < steps; step++) {
			float elapsed = static_cast<float>(step) * 0.001f;
			float envelope = std::sin(pi * elapsed / seconds);
			envelope = envelope * envelope;
			float t = static_cast<float>(timeUs) * 0.000001f;
			float x = envelope * (0.3f * std::sin(2.0f * pi * 2.0f * t) + 0.3f * std::sin(2.0f * pi * 8.0f * t));
			float y = envelope * (0.3f * std::sin(2.0f * pi * 8.0f * t + 1.0f) + 0.3f * std::sin(2.0f * pi * 2.0f * t + 2.0f));

			float since = elapsed - knockAtSec;
			if (since > 0.0f && since <= 0.015f) {
				x = x + knockSign * knockAmplitudeG * (since / tauSec) * std::exp(-since / tauSec);
			}
			Step(x, y, 1.0f);
		}
	}


private:
	float Noise() {
		seed = seed * 1664525u + 1013904223u;
		float unit = static_cast<float>(seed >> 8) / 16777216.0f * 2.0f - 1.0f;
		return unit * noiseG;
	}


	// Adds the impulse to the Z gravity baseline, or to X or Y, which have no gravity.
	void Emit(uint32 axis, float value) {
		float x = 0.0f;
		float y = 0.0f;
		float z = 1.0f;
		if (axis == 0) {
			x = value;
		} else if (axis == 1) {
			y = value;
		} else {
			z = z + value;
		}
		Step(x, y, z);
	}
};


// The settings the test firmware uses after calibration: the two calibrated faces, single taps at once.
TapDetector::Tuning FirmwareTuning(TapDetector::Face top, TapDetector::Face back) {
	TapDetector::Tuning tuning;
	tuning.classification = TapDetector::Classification::TwoFaces;
	tuning.twoFaces[0] = top;
	tuning.twoFaces[1] = back;
	tuning.doubleTapEnabled = false;
	return tuning;
}


bool HasRejection(const std::vector<TapDetector::Rejection>& rejections, TapDetector::Reason reason) {
	for (const TapDetector::Rejection& rejection : rejections) {
		if (rejection.reason == reason) {
			return true;
		}
	}
	return false;
}


std::vector<TapDetector::Tap> DrainTaps(TapRig& rig) {
	std::vector<TapDetector::Tap> taps;
	TapDetector::Tap tap;
	while (rig.detector.PopTap(tap)) {
		taps.push_back(tap);
	}
	return taps;
}


std::vector<TapDetector::Rejection> DrainRejections(TapRig& rig) {
	std::vector<TapDetector::Rejection> rejections;
	TapDetector::Rejection rejection;
	while (rig.detector.PopRejection(rejection)) {
		rejections.push_back(rejection);
	}
	return rejections;
}


void CheckSingleOnPlusZ() {
	TapRig rig;
	rig.Idle(0.5f);
	uint64 knockStart = rig.timeUs;
	rig.Knock(2, 1.0f, 0.8f, true);
	rig.Idle(0.6f);
	std::vector<TapDetector::Tap> taps = DrainTaps(rig);
	VHAL_CHECK(taps.size() == 1);
	if (taps.size() == 1) {
		VHAL_CHECK(taps[0].face == TapDetector::Face::PlusZ);
		VHAL_CHECK(taps[0].kind == TapDetector::Kind::Single);
		VHAL_CHECK(taps[0].timeUs >= knockStart && taps[0].timeUs <= knockStart + 3000);
		VHAL_CHECK(taps[0].peakGPerS > 150.0f);
	}
}


void CheckNegativeFace() {
	TapRig rig;
	rig.Idle(0.5f);
	rig.Knock(0, -1.0f, 0.8f, false);
	rig.Idle(0.6f);
	std::vector<TapDetector::Tap> taps = DrainTaps(rig);
	VHAL_CHECK(taps.size() == 1 && taps[0].face == TapDetector::Face::MinusX);
}


void CheckDoubleSameFace() {
	TapRig rig;
	rig.Idle(0.5f);
	rig.Knock(2, 1.0f, 0.8f, false);
	rig.Idle(0.2f);
	rig.Knock(2, 1.0f, 0.8f, false);
	rig.Idle(0.6f);
	std::vector<TapDetector::Tap> taps = DrainTaps(rig);
	VHAL_CHECK(taps.size() == 1);
	if (taps.size() == 1) {
		VHAL_CHECK(taps[0].kind == TapDetector::Kind::Double);
		VHAL_CHECK(taps[0].face == TapDetector::Face::PlusZ);
	}
}


// Two taps 0.6 s apart are outside the double window, so they are two singles.
void CheckTwoSingles() {
	TapRig rig;
	rig.Idle(0.5f);
	rig.Knock(2, 1.0f, 0.8f, false);
	rig.Idle(0.6f);
	rig.Knock(2, 1.0f, 0.8f, false);
	rig.Idle(0.6f);
	std::vector<TapDetector::Tap> taps = DrainTaps(rig);
	VHAL_CHECK(taps.size() == 2);
	if (taps.size() == 2) {
		VHAL_CHECK(taps[0].kind == TapDetector::Kind::Single && taps[1].kind == TapDetector::Kind::Single);
	}
}


// A second knock 30 ms after the first lands inside the refractory window; it must not become a second tap.
void CheckRefractoryNoSecond() {
	TapRig rig;
	rig.Idle(0.5f);
	rig.Knock(2, 1.0f, 0.8f, false);
	rig.Idle(0.03f);
	rig.Knock(2, 1.0f, 0.8f, false);
	rig.Idle(0.6f);
	std::vector<TapDetector::Tap> taps = DrainTaps(rig);
	VHAL_CHECK(taps.size() == 1 && taps[0].kind == TapDetector::Kind::Single);
}


// Ringing after the knock alone must produce one tap, and any re-triggered candidate is reported as refractory.
void CheckRingingIsOneTap() {
	TapRig rig;
	rig.Idle(0.5f);
	rig.Knock(2, 1.0f, 0.8f, true);
	rig.Idle(0.6f);
	std::vector<TapDetector::Tap> taps = DrainTaps(rig);
	VHAL_CHECK(taps.size() == 1);
}


// Shaking at 20 Hz with 0.5 g amplitude: the jerk passes the candidate level again and again. The first knock of the
// shake can still be accepted, because no earlier knock is in the window; the rest are rejected as moving.
void CheckRejectsMoving() {
	TapRig rig;
	rig.Idle(0.5f);
	rig.Shake(2.0f, 20.0f, 0.5f);
	rig.Idle(0.6f);
	std::vector<TapDetector::Tap> taps = DrainTaps(rig);
	std::vector<TapDetector::Rejection> rejections = DrainRejections(rig);
	VHAL_CHECK(taps.size() <= 1);
	bool moving = false;
	for (const TapDetector::Rejection& rejection : rejections) {
		moving = moving || rejection.reason == TapDetector::Reason::Moving;
	}
	VHAL_CHECK(moving);
}


// Peak 36 g/s: above the candidate level (30) but below the face threshold (60), so it is rejected as weak.
void CheckRejectsWeak() {
	TapRig rig;
	// The face threshold is set to 60 g/s, so this peak of about 32 g/s is above the candidate level but
	// below the face level and is rejected as weak.
	TapDetector::Tuning tuning;
	for (uint32 index = 0; index < 6; index++) {
		tuning.faceGPerS[index] = 60.0f;
	}
	rig.detector.SetTuning(tuning);
	rig.Idle(0.5f);
	rig.Knock(2, 1.0f, 0.15f, false);
	rig.Idle(0.6f);
	std::vector<TapDetector::Tap> taps = DrainTaps(rig);
	std::vector<TapDetector::Rejection> rejections = DrainRejections(rig);
	VHAL_CHECK(taps.empty());
	bool weak = false;
	for (const TapDetector::Rejection& rejection : rejections) {
		weak = weak || rejection.reason == TapDetector::Reason::Weak;
	}
	VHAL_CHECK(weak);
}


// With gyroscope confirmation on, a knock that does not rotate the device is rejected.
void CheckGyroConfirmation() {
	TapRig rig;
	TapDetector::Tuning tuning;
	tuning.gyroConfirmDps = 20.0f;
	rig.detector.SetTuning(tuning);
	rig.Idle(0.5f);
	rig.Knock(2, 1.0f, 0.8f, false);
	rig.Idle(0.6f);
	std::vector<TapDetector::Tap> taps = DrainTaps(rig);
	std::vector<TapDetector::Rejection> rejections = DrainRejections(rig);
	VHAL_CHECK(taps.empty());
	bool notConfirmed = false;
	for (const TapDetector::Rejection& rejection : rejections) {
		notConfirmed = notConfirmed || rejection.reason == TapDetector::Reason::GyroNotConfirmed;
	}
	VHAL_CHECK(notConfirmed);
}


// Hand sway of 0.3 g at 2 and 8 Hz with a light knock (peak about 32 g/s) in the middle: the knock is one tap on the top face,
// and the sway is neither a tap nor moving.
void CheckHandSwayLightKnock() {
	TapRig rig;
	rig.detector.SetTuning(FirmwareTuning(TapDetector::Face::PlusX, TapDetector::Face::PlusZ));
	rig.SwayWithKnock(2.0f, 1.0f, 0.15f, 1.0f);
	rig.Idle(0.1f);
	std::vector<TapDetector::Tap> taps = DrainTaps(rig);
	std::vector<TapDetector::Rejection> rejections = DrainRejections(rig);
	VHAL_CHECK(taps.size() == 1);
	if (taps.size() == 1) {
		VHAL_CHECK(taps[0].face == TapDetector::Face::PlusX);
		VHAL_CHECK(taps[0].peakGPerS >= 25.0f && taps[0].peakGPerS <= 40.0f);
	}
	VHAL_CHECK(!HasRejection(rejections, TapDetector::Reason::Moving));
}


// A knock on the top face and one on the back face (Z, the other axis) are two taps, each on its own face.
void CheckTopAndBackFaces() {
	TapRig rig;
	rig.detector.SetTuning(FirmwareTuning(TapDetector::Face::PlusX, TapDetector::Face::PlusZ));
	rig.Idle(0.5f);
	rig.Knock(0, 1.0f, 0.8f, false);
	rig.Idle(0.6f);
	rig.Knock(2, 1.0f, 0.8f, false);
	rig.Idle(0.6f);
	std::vector<TapDetector::Tap> taps = DrainTaps(rig);
	VHAL_CHECK(taps.size() == 2);
	if (taps.size() == 2) {
		VHAL_CHECK(taps[0].face == TapDetector::Face::PlusX);
		VHAL_CHECK(taps[1].face == TapDetector::Face::PlusZ);
	}
}


// The case rings after a top knock, on the back axis and on the side axis: the impact is the first lobe, so the knock stays
// on the top face.
void CheckRingingKeepsTopFace() {
	TapRig rig;
	rig.detector.SetTuning(FirmwareTuning(TapDetector::Face::PlusX, TapDetector::Face::PlusZ));
	rig.Idle(0.5f);
	rig.Knock(0, 1.0f, 0.8f, false);
	rig.Ring(2, 0.25f);
	rig.Ring(1, 0.25f);
	rig.Idle(0.6f);
	std::vector<TapDetector::Tap> taps = DrainTaps(rig);
	VHAL_CHECK(taps.size() == 1);
	if (taps.size() == 1) {
		VHAL_CHECK(taps[0].face == TapDetector::Face::PlusX);
	}
}


// A back knock followed by ringing on the top axis stays on the back face.
void CheckBackKnockRingingKeepsBackFace() {
	TapRig rig;
	rig.detector.SetTuning(FirmwareTuning(TapDetector::Face::PlusX, TapDetector::Face::PlusZ));
	rig.Idle(0.5f);
	rig.Knock(2, 1.0f, 0.8f, false);
	rig.Ring(0, 0.25f);
	rig.Idle(0.6f);
	std::vector<TapDetector::Tap> taps = DrainTaps(rig);
	VHAL_CHECK(taps.size() == 1 && taps[0].face == TapDetector::Face::PlusZ);
}


// The opposite face of a calibrated axis, and a knock on the side (Y, not calibrated), are other faces: no tap, and the
// rejection says so.
void CheckOtherFaceRejected() {
	TapRig rig;
	rig.detector.SetTuning(FirmwareTuning(TapDetector::Face::PlusX, TapDetector::Face::PlusZ));
	rig.Idle(0.5f);
	rig.Knock(2, -1.0f, 0.8f, false);
	rig.Idle(0.6f);
	rig.Knock(1, 1.0f, 0.8f, false);
	rig.Idle(0.6f);
	std::vector<TapDetector::Tap> taps = DrainTaps(rig);
	std::vector<TapDetector::Rejection> rejections = DrainRejections(rig);
	VHAL_CHECK(taps.empty());
	VHAL_CHECK(HasRejection(rejections, TapDetector::Reason::OtherFace));
}


// Single taps are reported at once (not after the 350 ms double window), and two knocks 0.3 s apart are two singles.
void CheckSingleIsImmediate() {
	TapRig rig;
	rig.detector.SetTuning(FirmwareTuning(TapDetector::Face::PlusX, TapDetector::Face::PlusZ));
	rig.Idle(0.5f);
	rig.Knock(0, 1.0f, 0.8f, false);
	rig.Idle(0.1f);
	std::vector<TapDetector::Tap> first = DrainTaps(rig);
	VHAL_CHECK(first.size() == 1 && first[0].kind == TapDetector::Kind::Single);

	rig.Idle(0.2f);
	rig.Knock(0, 1.0f, 0.8f, false);
	rig.Idle(0.1f);
	std::vector<TapDetector::Tap> second = DrainTaps(rig);
	VHAL_CHECK(second.size() == 1 && second[0].kind == TapDetector::Kind::Single);
}


// Start-to-start gaps of 60 ms (inside the 80 ms refractory window: ringing, not a tap) and 100 ms (a second tap).
void CheckRefractoryBoundary() {
	TapRig close;
	close.detector.SetTuning(FirmwareTuning(TapDetector::Face::PlusX, TapDetector::Face::PlusZ));
	close.Idle(0.5f);
	close.Knock(0, 1.0f, 0.8f, false);
	close.Idle(0.045f);
	close.Knock(0, 1.0f, 0.8f, false);
	close.Idle(0.5f);
	VHAL_CHECK(DrainTaps(close).size() == 1);

	TapRig apart;
	apart.detector.SetTuning(FirmwareTuning(TapDetector::Face::PlusX, TapDetector::Face::PlusZ));
	apart.Idle(0.5f);
	apart.Knock(0, 1.0f, 0.8f, false);
	apart.Idle(0.085f);
	apart.Knock(0, 1.0f, 0.8f, false);
	apart.Idle(0.5f);
	VHAL_CHECK(DrainTaps(apart).size() == 2);
}


// Five knocks 120 ms apart: the first two are taps, the third and the rest are a burst and count as moving.
void CheckKnockBurstIsMoving() {
	TapRig rig;
	rig.detector.SetTuning(FirmwareTuning(TapDetector::Face::PlusX, TapDetector::Face::PlusZ));
	rig.Idle(0.5f);
	for (uint32 index = 0; index < 5; index++) {
		rig.Knock(0, 1.0f, 0.8f, false);
		rig.Idle(0.105f);
	}
	rig.Idle(0.5f);
	std::vector<TapDetector::Tap> taps = DrainTaps(rig);
	std::vector<TapDetector::Rejection> rejections = DrainRejections(rig);
	VHAL_CHECK(taps.size() <= 2);
	VHAL_CHECK(HasRejection(rejections, TapDetector::Reason::Moving));
}


// Thresholds: a knock of about 20 g/s is below the default 25 g/s and gives no tap; one of about 32 g/s is above it. With a
// threshold of 40 g/s the 32 g/s knock is no tap.
void CheckThresholdEdges() {
	TapRig low;
	low.detector.SetTuning(FirmwareTuning(TapDetector::Face::PlusX, TapDetector::Face::PlusZ));
	low.Idle(0.5f);
	low.Knock(0, 1.0f, 0.095f, false);
	low.Idle(0.5f);
	VHAL_CHECK(DrainTaps(low).empty());
	low.Knock(0, 1.0f, 0.15f, false);
	low.Idle(0.5f);
	VHAL_CHECK(DrainTaps(low).size() == 1);

	TapRig high;
	TapDetector::Tuning tuning = FirmwareTuning(TapDetector::Face::PlusX, TapDetector::Face::PlusZ);
	tuning.candidateGPerS = 40.0f;
	for (uint32 index = 0; index < 6; index++) {
		tuning.faceGPerS[index] = 40.0f;
	}
	high.detector.SetTuning(tuning);
	high.Idle(0.5f);
	high.Knock(0, 1.0f, 0.15f, false);
	high.Idle(0.5f);
	VHAL_CHECK(DrainTaps(high).empty());
}


}


int main() {
	CheckSingleOnPlusZ();
	CheckNegativeFace();
	CheckDoubleSameFace();
	CheckTwoSingles();
	CheckRefractoryNoSecond();
	CheckRingingIsOneTap();
	CheckRejectsMoving();
	CheckRejectsWeak();
	CheckGyroConfirmation();
	CheckHandSwayLightKnock();
	CheckTopAndBackFaces();
	CheckRingingKeepsTopFace();
	CheckBackKnockRingingKeepsBackFace();
	CheckOtherFaceRejected();
	CheckSingleIsImmediate();
	CheckRefractoryBoundary();
	CheckKnockBurstIsMoving();
	CheckThresholdEdges();
	return VhalCheck::Finish("TestTapDetector");
}
