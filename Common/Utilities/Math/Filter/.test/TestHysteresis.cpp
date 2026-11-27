#include <VHAL.h>
#include <Utilities/Math/Filter/Hysteresis.h>
#include <VhalCheck.h>


// Host tests of the hysteresis window: the examples of the wiki, with a symmetric window and with each one-sided pull.


namespace {


void CheckWikiExamples() {
	Hysteresis<float> hyst(2.0f);
	VHAL_CHECK(hyst.Get(10.0f) == 10.0f);
	VHAL_CHECK(hyst.Get(11.0f) == 10.0f);
	VHAL_CHECK(hyst.Get(13.0f) == 13.0f);

	Hysteresis<int> up(3, Hysteresis<int>::PullType::PullUp);
	VHAL_CHECK(up.Get(50) == 50);
	VHAL_CHECK(up.Get(51) == 51);
	VHAL_CHECK(up.Get(49) == 51);

	Hysteresis<float> down(1.0f, Hysteresis<float>::PullType::PullDown);
	VHAL_CHECK(down.Get(100.0f) == 100.0f);
	VHAL_CHECK(down.Get(99.5f) == 99.5f);
	VHAL_CHECK(down.Get(100.5f) == 99.5f);
}


}


int main() {
	CheckWikiExamples();
	return VhalCheck::Finish("TestHysteresis");
}
