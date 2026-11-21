#include <DemoUSBStorage/DemoUSBStorage.h>
#include <DemoUSBStorage/Descriptors/StorageDescriptors.h>
#include <DemoUSBStorage/Disk/CountingBlockDevice.h>
#include <DemoUSBStorage/Disk/StorageVolume.h>
#include <Drivers/Interface/USB/Class/MSC/MscFunction.h>
#include <Drivers/Interface/USB/Descriptor/USBSerialNumber.h>
#include <Drivers/Interface/USB/Device/USBDevice.h>
#include <array>
#include <cstddef>
#include <string_view>


// The result of BSP::Init() for USART1, readable over SWD like the other demos'. ok means the console is working.
volatile ResultStatus storageConsoleStatus __attribute__((used)) = ResultStatus::ok;


namespace DemoUSBStorage {
constexpr uint64 StatusIntervalMs = 1000;
// The data buffer of the mass storage function: one data stage of one transfer, so a main loop pass moves at most this many bytes. It is a
// multiple of the packet size (64) and of the block size (512). It is RAM, so it is part of the budget in StorageVolume.h.
constexpr std::size_t MscBufferSize = 2048;


// The revision field of INQUIRY is four characters: the device release as BCD, so DeviceRelease 0x0100 is "1.00", the text the host shows.
constexpr std::array<char, 4> MakeRevision(uint16 release) {
	return std::array<char, 4>{
		static_cast<char>('0' + ((release >> 8) & 0x0F)),
		'.',
		static_cast<char>('0' + ((release >> 4) & 0x0F)),
		static_cast<char>('0' + (release & 0x0F))
	};
}


// The identity the host reads in INQUIRY. The strings refer to static storage, as MscIdentity requires.
static constexpr std::array<char, 4> storageRevision = MakeRevision(DemoProducts::DeviceRelease);
static constexpr MscIdentity storageIdentity{
	DemoProducts::StorageVendor,
	DemoProducts::StorageProduct,
	std::string_view(storageRevision.data(), storageRevision.size()),
	true
};


// The mass storage function of the demo. Every configuration makes the RAM disk present again: after "Safely Remove" the host
// enumerates the board again on a re-plug, and the disk comes back with the contents it had, as a USB stick does. The flag is one
// store, and then the base reads the configuration.
class StorageMscFunction : public MscFunction<MscBufferSize> {
private:
	StorageDisk& storage;


public:
	StorageMscFunction(const USBFunctionLayout& layout, StorageDisk& disk, IBlockDevice& device, const MscIdentity& identity)
		: MscFunction<MscBufferSize>(layout, device, identity),
		  storage(disk) {
	}


	ResultStatus OnConfigured(USBDeviceContext& context) override {
		storage.SetPresent(true);
		return MscFunction<MscBufferSize>::OnConfigured(context);
	}
};


static StorageDisk disk(StorageImage::Bytes);
static CountingBlockDevice counting(disk);
static StorageMscFunction mscFunction(StorageUsb::Function(0), disk, counting, storageIdentity);
static std::array<USBFunction*, 1> functions = { &mscFunction };
static USBDeviceAdapter::Parameters usbParameters;
static USBDevice usbDevice(BSP::usbAdapter, StorageUsb::Set, functions);


// The MCU's 96-bit unique ID as the serial number. Its hex text is 24 characters, and the mass storage class asks for at least 12 hex
// characters on a Bulk-Only device, so the host gets a serial number it can use to tell boards apart. The descriptor is kept in static
// storage, as the device requires (BenchDescriptors.h has the same rule).
static std::array<uint8, 2 + 4 * BSP::UniqueIdSize> serialDescriptor{};


// Gives the device its serial number string from the unique ID. Called once, before the first Start().
static ResultStatus SetUniqueSerialNumber() {
	std::array<uint8, BSP::UniqueIdSize> uniqueId{};
	BSP::ReadUniqueId(uniqueId);

	ResultStatus status = MakeUSBSerialNumberDescriptor(uniqueId, serialDescriptor);
	if (status != ResultStatus::ok) {
		return status;
	}

	return usbDevice.SetSerialNumber(serialDescriptor);
}


// Called from the interrupt when the host ejects the disk (START STOP UNIT with LOEJ). The disk is taken away at once. The handler is one
// store. The disk comes back with the next configuration (StorageMscFunction::OnConfigured), so a re-plug finds it.
static void OnEject() {
	disk.SetPresent(false);
}


static void PrintStatus() {
	const char* stateName = USBDeviceStateName(usbDevice.GetState());

	const char* mediumName = "absent";
	if (counting.IsPresent() == ResultStatus::ok) {
		mediumName = "present";
	}

	// The line goes to the console ring, which never waits for USART1. The counts are totals since reset.
	System::console
		<< "t=" << System::GetMs()
		<< "ms state=" << stateName
		<< " addr=" << static_cast<uint32>(usbDevice.GetAddress())
		<< " medium=" << mediumName
		<< " blocksRead=" << counting.GetBlocksRead()
		<< " blocksWritten=" << counting.GetBlocksWritten()
		<< " usbErr=" << usbDevice.GetErrorCount()
		<< Console::endl;
}


void Run(ResultStatus consoleStatus) {
	storageConsoleStatus = consoleStatus;

	if (SetUniqueSerialNumber() != ResultStatus::ok) {
		System::console << "USB serial number failed" << Console::endl;
	}

	if (mscFunction.SetEjectDelegate(MscFunction<MscBufferSize>::EjectDelegate::Bind<&OnEject>()) != ResultStatus::ok) {
		System::console << "eject delegate failed" << Console::endl;
	}

	ResultStatus status = usbDevice.Start(usbParameters);
	if (status == ResultStatus::ok) {
		System::console << "USB storage started" << Console::endl;
	} else {
		System::console << "USB start failed" << Console::endl;
	}

	uint64 nextStatusMs = System::GetMs() + StatusIntervalMs;
	while (true) {
		usbDevice.Process();

		if (System::GetMs() >= nextStatusMs) {
			nextStatusMs += StatusIntervalMs;
			PrintStatus();
		}
	}
}
}
