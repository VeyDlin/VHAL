#pragma once
#include <DemoUSBComposite/Function/CompositeFunctions.h>
#include <Utilities/Console/Print.h>
#include <algorithm>
#include <span>


namespace DemoUSBComposite {
	// Text to the serial port, with the formatting of System::console (Print). The text is queued only while a terminal is open (DTR),
	// so nothing piles up while nobody reads it. When the transmit ring is full, the rest of the text is dropped and counted, as the
	// console ring does: the main loop never waits for the host.
	class CdcPrint : public Print {
	private:
		// The longest piece one port call takes: one full-speed bulk packet.
		static constexpr size_t WriteChunk = 64;

		SerialPort& port;
		uint32 dropped = 0;


	public:
		explicit CdcPrint(SerialPort& serialPort)
			: port(serialPort) {
		}


		// Bytes of text the transmit ring refused, since start.
		uint32 GetDropped() const {
			return dropped;
		}


		// Defined in CdcPrint.cpp. Inlined at every print call site at -O3 it costs about 29 KB of flash, so it stays out of line.
		void WriteRaw(const char* data, size_t size) override;
	};
}
