#include <VHAL.h>
#include <Utilities/Hardware/BatteryLevel/BatteryLevel.h>
#include <VhalCheck.h>
#include <initializer_list>


// Host tests of the battery level curve: the endpoints and the middle of a Li-ion profile, the monotonic fall of the division with the
// voltage, construction and destruction in a loop, and an empty profile, with and without jitter.


namespace {


BatteryLevel<float> MakeLiIon() {
	return BatteryLevel<float>({
		{ 4.20f, 100 },
		{ 3.80f, 90 },
		{ 3.55f, 20 },
		{ 3.10f, 10 },
		{ 3.00f, 0 }
	});
}


void CheckCurveEndpoints() {
	BatteryLevel<float> high = MakeLiIon();
	VHAL_CHECK_EQ(high.GetDivision(4.5f), 10);

	BatteryLevel<float> top = MakeLiIon();
	VHAL_CHECK_EQ(top.GetDivision(4.2f), 10);

	BatteryLevel<float> bottom = MakeLiIon();
	VHAL_CHECK_EQ(bottom.GetDivision(3.0f), 0);

	BatteryLevel<float> low = MakeLiIon();
	VHAL_CHECK_EQ(low.GetDivision(2.5f), 0);
}


void CheckMidpoint() {
	BatteryLevel<float> battery = MakeLiIon();
	VHAL_CHECK_EQ(battery.GetDivision(4.0f), 9);
}


void CheckDescendingMonotonic() {
	BatteryLevel<float> battery = MakeLiIon();
	int previous = 11;
	for (int step = 0; step <= 1300; ++step) {
		float voltage = 4.3f - 0.005f * step;
		int division = battery.GetDivision(voltage);
		bool monotonic = division <= previous;
		VHAL_CHECK(monotonic);
		if (!monotonic) {
			return;
		}
		previous = division;
	}
}


void CheckNoLeak() {
	for (int i = 0; i < 1000; ++i) {
		BatteryLevel<float> battery({ { 4.2f, 100 }, { 3.0f, 0 } });
		(void)battery.GetDivision(3.5f);
	}
}


void CheckEmptyProfileReturnsZero() {
	BatteryLevel<float> battery(std::initializer_list<BatteryLevel<float>::BatteryProfilePoint>{});
	VHAL_CHECK_EQ(battery.GetDivision(3.5f), 0);
	VHAL_CHECK_EQ(battery.GetDivision(4.5f), 0);
}


void CheckEmptyProfileWithJitterReturnsZero() {
	BatteryLevel<float> battery(std::initializer_list<BatteryLevel<float>::BatteryProfilePoint>{}, 10, 1, 0.05f);
	VHAL_CHECK_EQ(battery.GetDivision(3.5f), 0);
}


}


int main() {
	CheckCurveEndpoints();
	CheckMidpoint();
	CheckDescendingMonotonic();
	CheckNoLeak();
	CheckEmptyProfileReturnsZero();
	CheckEmptyProfileWithJitterReturnsZero();
	return VhalCheck::Finish("TestBatteryLevel");
}
