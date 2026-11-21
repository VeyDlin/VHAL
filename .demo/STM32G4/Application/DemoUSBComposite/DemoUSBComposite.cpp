#include <DemoUSBComposite/DemoUSBComposite.h>
#include <DemoUSBComposite/CommandLine/CompositeCommandLine.h>
#include <DemoUSBComposite/Descriptors/CompositeDescriptors.h>
#include <DemoUSBComposite/Function/CdcPrint.h>
#include <DemoUSBComposite/Function/CompositeFunctions.h>
#include <DemoUSBComposite/Status/CompositeStatus.h>
#include <Drivers/Interface/USB/Descriptor/USBSerialNumber.h>
#include <Drivers/Interface/USB/Device/USBDevice.h>
#include <algorithm>
#include <array>
#include <span>


// The result of BSP::Init() for USART1, readable over SWD like the bench's. ok means the console is working.
volatile ResultStatus compositeConsoleStatus __attribute__((used)) = ResultStatus::ok;


namespace DemoUSBComposite {
	constexpr uint64 StatusIntervalMs = 1000;
	// Endpoint 0 and the seven endpoint numbers the builder assigns. The G4 port opens numbers below endpointCount only.
	constexpr uint8 EndpointCount = 8;
	// Vendor bytes looped per main loop pass. The ring holds more than this, so a busy host is served over several passes.
	constexpr uint16 LoopbackChunk = 64;


	// The functions, in the order of the descriptors (CompositeUsb::Function(index)). The HID functions use the report descriptors of the
	// boot devices; the keyboard and the mouse wrap them.
	static KeyboardFunction keyboardFunction(CompositeUsb::Function(0), CompositeKeyboardReport::Info);
	static Keyboard keyboard(keyboardFunction);
	static MouseFunction mouseFunction(CompositeUsb::Function(1), CompositeMouseReport::Info);
	static Mouse mouse(mouseFunction);
	static SerialPort serialPort(CompositeUsb::Function(2));
	static VendorChannel vendorChannel(CompositeUsb::Function(3));
	static CdcPrint serialOut(serialPort);
	// The vendor bytes looped back since start, in the status line and the status command.
	static uint32 loopedBytes = 0;
	static std::array<USBFunction*, 4> functions = { &keyboardFunction, &mouseFunction, &serialPort, &vendorChannel };
	static USBDevice usbDevice(BSP::usbAdapter, CompositeUsb::Set, functions);
	static CompositeCommandLine commandLine(serialPort, serialOut, keyboard, mouse, usbDevice, loopedBytes);


	// The MCU's unique ID as the serial number, so each board keeps its number and several boards are told apart. The descriptor is kept
	// in static storage, as the device requires (BenchDescriptors.h has the same rule).
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


	// The vendor channel loops every received byte back. It takes only as many bytes as the transmit ring has room for, so none is lost.
	static void Loopback() {
		std::array<uint8, LoopbackChunk> buffer{};
		uint16 count = std::min<uint16>(vendorChannel.GetWritableCount(), vendorChannel.GetReadableCount());
		count = std::min<uint16>(count, LoopbackChunk);
		uint16 read = vendorChannel.Read(std::span<uint8>(buffer.data(), count));
		loopedBytes += vendorChannel.Write(std::span<const uint8>(buffer.data(), read));
	}


	// The status line on System::console, once a second, the same fields as the status command on the serial port.
	static void PrintStatus() {
		Print& out = System::console;
		out.Write("t=");
		out.Write(System::GetMs());
		out.Write("ms ");
		WriteUsbState(out, usbDevice, keyboard);
		out.Write(" dtr=");
		out.Write(serialPort.IsTerminalOpen());
		out.Write(" looped=");
		out.Write(loopedBytes);
		out.Write(" cdcDropped=");
		out.Write(serialOut.GetDropped());
		out.Write(" usbErr=");
		out.Write(usbDevice.GetErrorCount());
		out.Line();
	}


	void Run(ResultStatus consoleStatus) {
		compositeConsoleStatus = consoleStatus;

		if (SetUniqueSerialNumber() != ResultStatus::ok) {
			System::console << "USB serial number failed" << Console::endl;
		}

		USBDeviceAdapter::Parameters parameters;
		parameters.endpointCount = EndpointCount;
		ResultStatus status = usbDevice.Start(parameters);
		if (status == ResultStatus::ok) {
			System::console << "USB composite started" << Console::endl;
		} else {
			System::console << "USB start failed" << Console::endl;
		}

		uint64 nextStatusMs = System::GetMs() + StatusIntervalMs;
		while (true) {
			usbDevice.Process();
			commandLine.Process();
			Loopback();
			mouse.Flush();

			if (System::GetMs() >= nextStatusMs) {
				nextStatusMs += StatusIntervalMs;
				PrintStatus();
			}
		}
	}
}
