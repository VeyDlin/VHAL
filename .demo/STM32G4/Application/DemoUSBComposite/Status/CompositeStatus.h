#pragma once
#include <DemoUSBComposite/Function/CompositeFunctions.h>
#include <Drivers/Interface/USB/Device/USBDevice.h>
#include <Utilities/Console/Print.h>


// The status text of the composite. The once-a-second line on System::console and the status command on the serial port both print it,
// so the two read the same.
namespace DemoUSBComposite {
	// The keyboard LED bits of the host's output report.
	constexpr uint8 NumLockBit = 0x01;
	constexpr uint8 CapsLockBit = 0x02;
	constexpr uint8 ScrollLockBit = 0x04;


	// Writes the USB state, the address and the keyboard LEDs, without a line end.
	inline void WriteUsbState(Print& out, const USBDevice& device, const Keyboard& keyboard) {
		uint8 leds = keyboard.Leds();
		out.Write("state=");
		out.Write(USBDeviceStateName(device.GetState()));
		out.Write(" addr=");
		out.Write(static_cast<uint32>(device.GetAddress()));
		out.Write(" numLock=");
		out.Write((leds & NumLockBit) != 0);
		out.Write(" capsLock=");
		out.Write((leds & CapsLockBit) != 0);
		out.Write(" scrollLock=");
		out.Write((leds & ScrollLockBit) != 0);
	}
}
