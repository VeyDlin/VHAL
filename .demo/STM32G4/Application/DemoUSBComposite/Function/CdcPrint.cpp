#include <DemoUSBComposite/Function/CdcPrint.h>
#include <algorithm>
#include <span>


namespace DemoUSBComposite {
	void CdcPrint::WriteRaw(const char* data, size_t size) {
		if (!port.IsTerminalOpen()) {
			return;
		}

		// One packet per port call, as the port's own rings copy: each call's copy stays short, so the masked interrupts stay short too.
		const uint8* bytes = reinterpret_cast<const uint8*>(data);
		size_t position = 0;
		while (position < size) {
			size_t chunk = std::min<size_t>(size - position, WriteChunk);
			uint16 queued = port.Write(std::span<const uint8>(bytes + position, chunk));
			position += queued;
			if (queued < chunk) {
				dropped = dropped + static_cast<uint32>(size - position);
				return;
			}
		}
	}
}
