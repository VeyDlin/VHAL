#include <VHAL.h>
#include "MscHost.h"
#include "SimAssert.h"
#include "SimHost.h"
#include <array>
#include <cstddef>
#include <span>


// The presence of a medium that comes back in a new configuration. The host ejects the medium, the board stays powered, and the host
// enumerates the device again. The application makes the medium present again in its OnConfigured(), as the storage demo does, and
// the host's first TEST UNIT READY after the configuration must see it. The function reads the presence in Process(), so the first
// CBW of a configuration waits for that read, and the answer does not come from the presence sampled before the eject.


namespace {


using Disk = RamBlockDevice<512, 128>;
using MscTest::BotHost;
using MscTest::OutAddress;
using MscTest::Outcome;


inline constexpr std::array<uint8, 6> TestUnitReadyCdb{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
inline constexpr std::array<uint8, 6> StopAndEjectCdb{ 0x1B, 0x00, 0x00, 0x00, 0x02, 0x00 };


// The function of the storage demo: its configuration makes the RAM disk present, and then the base takes the configuration.
class RePlugFunction : public MscFunction<512> {
private:
	Disk& medium;


public:
	RePlugFunction(const USBFunctionLayout& layout, Disk& disk, IBlockDevice& device, const MscIdentity& identity)
		: MscFunction<512>(layout, device, identity),
		  medium(disk) {
	}


	ResultStatus OnConfigured(USBDeviceContext& context) override {
		medium.SetPresent(true);
		return MscFunction<512>::OnConfigured(context);
	}
};


// One device with the RePlugFunction, and the eject that the storage demo's eject delegate does.
struct RePlugDevice {
	SimUSBDeviceAdapter sim;
	Disk disk;
	RePlugFunction function;
	std::array<USBFunction*, 1> functions;
	USBDevice device;


	RePlugDevice()
		: function(MscTest::MscUsb::Function(0), disk, disk, MscTest::Identity),
		  functions{ &function },
		  device(sim, MscTest::MscUsb::Set, functions) {
	}


	// The eject delegate: the medium goes away, as the storage demo's OnEject() does.
	void OnEject() {
		disk.SetPresent(false);
	}


	// Start, then the first attach.
	ResultStatus Attach() {
		ResultStatus status = device.Start(USBDeviceAdapter::Parameters());
		if (status != ResultStatus::ok) {
			return status;
		}

		return Enumerate(1);
	}


	// What a host does on each attach: a bus reset, the device descriptor, SET_ADDRESS and SET_CONFIGURATION.
	ResultStatus Enumerate(uint8 address) {
		sim.InjectBusReset();
		std::array<uint8, 18> descriptor{};
		uint32 received = 0;
		ResultStatus status = SimHost::Control(sim, SimHost::MakeSetup(0x80, 6, 0x0100, 0x0000, 18), std::span<const uint8>(), descriptor, received);
		if (status != ResultStatus::ok) {
			return status;
		}

		status = SimHost::Control(sim, SimHost::MakeSetup(0x00, 5, address, 0x0000, 0));
		if (status != ResultStatus::ok) {
			return status;
		}

		return SimHost::Control(sim, SimHost::MakeSetup(0x00, 9, 1, 0x0000, 0));
	}
};


// The host ejects the medium, then the board is re-plugged and the medium comes back in the configuration. The first TEST UNIT READY
// of the new configuration passes. No Process() runs before the host's first command, so the presence is the one the configuration
// read: the CBW is not armed before that.
void TestUnitReadyAfterReplugSeesTheMedium() {
	RePlugDevice fixture;
	SIM_EXPECT_EQ(fixture.Attach(), ResultStatus::ok);
	fixture.function.SetEjectDelegate(MscFunction<512>::EjectDelegate::Bind<RePlugDevice, &RePlugDevice::OnEject>(&fixture));
	BotHost host(fixture.sim, fixture.device);

	Outcome eject = host.Command(0x10, StopAndEjectCdb, 0, false, {}, {});
	SIM_EXPECT_EQ(eject.csw.status, MscStatusPassed);
	Outcome absent = host.Command(0x11, TestUnitReadyCdb, 0, false, {}, {});
	SIM_EXPECT_EQ(absent.csw.status, MscStatusFailed);

	SIM_EXPECT_EQ(fixture.Enumerate(2), ResultStatus::ok);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(OutAddress));

	Outcome present = host.Command(0x12, TestUnitReadyCdb, 0, false, {}, {});
	SIM_EXPECT_EQ(present.cswStatus, ResultStatus::ok);
	SIM_EXPECT_EQ(present.csw.status, MscStatusPassed);
}


}


int main() {
	TestUnitReadyAfterReplugSeesTheMedium();
	return SimTest::Finish("TestMscPresence");
}
