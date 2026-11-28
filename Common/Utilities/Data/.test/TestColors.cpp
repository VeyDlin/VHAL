#include <VHAL.h>
#include <VhalCheck.h>
#include <Utilities/Data/Colors/Colors.h>

#include <cstdint>


// Host tests of the colour types: the conversion of float to 8-bit channels with rounding and clamping, the round trip of every
// channel value, the scaling and the channel arithmetic that saturates, and the gamma profiles.


using Colors::FRgb;
using Colors::GammaProfile;
using Colors::URgb;


void UrgbFromFrgbRoundsToNearest() {
	URgb converted(FRgb(1.0f, 0.5f, 0.0f));
	VHAL_CHECK(converted == URgb(255, 128, 0));
}


void UrgbFrgbRoundTripAllChannelValues() {
	for (int v = 0; v <= 255; v++) {
		URgb original(static_cast<uint8>(v), static_cast<uint8>(v), static_cast<uint8>(v));
		FRgb asFloat(original);
		URgb back(asFloat);
		VHAL_CHECK(back == original);
		if (back != original) {
			break;
		}
	}
}


void UrgbScaleRounds() {
	URgb scaled = URgb(100, 200, 50) * 0.5f;
	VHAL_CHECK(scaled == URgb(50, 100, 25));
}


void UrgbFromFrgbOutOfRangeClamps() {
	URgb converted(FRgb(1.5f, -0.2f, 0.0f));
	VHAL_CHECK(converted == URgb(255, 0, 0));
}


void GammaLinearIsIdentity() {
	URgb input(10, 20, 30);
	VHAL_CHECK(GammaProfile::Linear().Apply(input) == input);
}


void GammaSrgbKeepsEndpoints() {
	GammaProfile profile = GammaProfile::sRGB();
	VHAL_CHECK(profile.Apply(URgb(255, 255, 255)) == URgb(255, 255, 255));
	VHAL_CHECK(profile.Apply(URgb(0, 0, 0)) == URgb(0, 0, 0));
}


void UrgbAddSaturatesPerChannel() {
	URgb sum = URgb(200, 100, 0) + URgb(100, 200, 10);
	VHAL_CHECK(sum == URgb(255, 255, 10));

	URgb accumulated(200, 100, 0);
	accumulated += URgb(100, 200, 10);
	VHAL_CHECK(accumulated == URgb(255, 255, 10));
}


void UrgbSubtractSaturatesAtZero() {
	URgb difference = URgb(10, 200, 5) - URgb(20, 100, 6);
	VHAL_CHECK(difference == URgb(0, 100, 0));

	URgb accumulated(10, 200, 5);
	accumulated -= URgb(20, 100, 6);
	VHAL_CHECK(accumulated == URgb(0, 100, 0));
}


int main() {
	UrgbFromFrgbRoundsToNearest();
	UrgbFrgbRoundTripAllChannelValues();
	UrgbScaleRounds();
	UrgbFromFrgbOutOfRangeClamps();
	GammaLinearIsIdentity();
	GammaSrgbKeepsEndpoints();
	UrgbAddSaturatesPerChannel();
	UrgbSubtractSaturatesAtZero();

	return VhalCheck::Finish("TestColors");
}
