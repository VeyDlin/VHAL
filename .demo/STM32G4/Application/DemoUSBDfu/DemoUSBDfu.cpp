#include <DemoUSBDfu/DemoUSBDfu.h>
#include <DemoUSBDfu/Descriptors/DfuDescriptors.h>
#include <DemoUSBDfu/Status/DfuStatusText.h>
#include <DemoUSBDfu/Target/DfuRegionTarget.h>
#include <Drivers/Interface/USB/Class/DFU/DfuFunction.h>
#include <Drivers/Interface/USB/Descriptor/USBSerialNumber.h>
#include <Drivers/Interface/USB/Device/USBDevice.h>
#include <Utilities/Checksum/CRC/Crc.h>
#include <array>
#include <cstdint>


// The result of BSP::Init() for USART1, readable over SWD like the other demos'. ok means the console is working.
volatile ResultStatus dfuConsoleStatus __attribute__((used)) = ResultStatus::ok;


namespace DemoUSBDfu {
constexpr uint64 StatusIntervalMs = 1000;


// The FLASH adapter of the G4, and the DFU mode function over the region target. The adapter's range is set to the region in Run(),
// so every erase and program is refused outside it.
static AFLASH regionFlash = { FLASH };
static DfuRegionTarget regionTarget(regionFlash);
static DfuFunction<DfuTransferSize> dfuFunction(DfuUsb::Function(0), regionTarget);
static std::array<USBFunction*, 1> functions = { &dfuFunction };
static USBDevice usbDevice(BSP::usbAdapter, DfuUsb::Set, functions);


// The MCU's unique ID as the serial number, so each board keeps its number. The descriptor is kept in static storage, as the device
// requires (BenchDescriptors.h has the same rule).
static std::array<uint8, 2 + 4 * BSP::UniqueIdSize> serialDescriptor{};


// The last completed download, for the status line. lastBytes stays 0 until the first download completes.
static uint32 lastBytes = 0;
static uint32 lastCrc = 0;


// Gives the adapter the region as its range. Returns the adapter's result, so Run() can refuse to start the device without it.
static ResultStatus SetUpRegion() {
	AFLASH::Parameters parameters = { RegionStart, RegionStart + RegionSize };
	return regionFlash.SetParameters(parameters);
}


// Reads the unique ID and gives the device its serial number string. Called once, before the first Start().
static ResultStatus SetUniqueSerialNumber() {
	std::array<uint8, BSP::UniqueIdSize> uniqueId{};
	BSP::ReadUniqueId(uniqueId);

	ResultStatus status = MakeUSBSerialNumberDescriptor(uniqueId, serialDescriptor);
	if (status != ResultStatus::ok) {
		return status;
	}

	return usbDevice.SetSerialNumber(serialDescriptor);
}


// The download's Manifest() ran in usbDevice.Process(), which is in the main loop, so the CRC runs here and not in an interrupt. It
// covers the bytes the download wrote, from the region start: those are the bytes of the file the PC sent, so the printed CRC equals
// the CRC of the file (a zlib or Windows CRC-32 of the same bytes).
static void ReportDownload() {
	uint32 bytes = 0;
	if (!regionTarget.TakeManifestedBytes(bytes)) {
		return;
	}

	const void* region = reinterpret_cast<const void*>(static_cast<uintptr_t>(RegionStart));
	lastBytes = bytes;
	lastCrc = Crc::Calculate<uint32, 32>(region, bytes, Crc::CRC_32());

	Print& out = System::console;
	out.Write("download complete bytes=");
	out.Write(lastBytes);
	out.Write(" crc32=");
	WriteHex32(out, lastCrc);
	out.Line();
}


// The status line on System::console, once a second: the USB state, the DFU state and status, and the byte counts since start.
static void PrintStatus() {
	Print& out = System::console;
	out.Write("t=");
	out.Write(System::GetMs());
	out.Write("ms usb=");
	out.Write(USBDeviceStateName(usbDevice.GetState()));
	out.Write(" addr=");
	out.Write(static_cast<uint32>(usbDevice.GetAddress()));
	out.Write(" dfu=");
	out.Write(DfuStateName(dfuFunction.GetState()));
	out.Write(" status=");
	out.Write(DfuStatusName(dfuFunction.GetStatus()));
	out.Write(" downloaded=");
	out.Write(regionTarget.GetDownloadedBytes());
	out.Write(" uploaded=");
	out.Write(regionTarget.GetUploadedBytes());
	out.Write(" last=");
	if (lastBytes == 0) {
		out.Write("none");
	} else {
		out.Write(lastBytes);
		out.Write("B crc32=");
		WriteHex32(out, lastCrc);
	}

	out.Line();
}


void Run(ResultStatus consoleStatus) {
	dfuConsoleStatus = consoleStatus;

	if (SetUniqueSerialNumber() != ResultStatus::ok) {
		System::console << "USB serial number failed" << Console::endl;
	}

	Print& out = System::console;
	out.Write("DFU region ");
	WriteHex32(out, RegionStart);
	out.Write(" size=");
	out.Write(RegionSize);
	out.Line();

	bool started = false;
	if (SetUpRegion() != ResultStatus::ok) {
		System::console << "DFU region setup failed" << Console::endl;
	} else {
		USBDeviceAdapter::Parameters parameters;
		ResultStatus status = usbDevice.Start(parameters);
		if (status == ResultStatus::ok) {
			started = true;
			System::console << "USB DFU started" << Console::endl;
		} else {
			System::console << "USB start failed" << Console::endl;
		}
	}

	uint64 nextStatusMs = System::GetMs() + StatusIntervalMs;
	while (true) {
		if (started) {
			usbDevice.Process();
		}

		ReportDownload();

		if (System::GetMs() >= nextStatusMs) {
			nextStatusMs += StatusIntervalMs;
			PrintStatus();
		}
	}
}
}
