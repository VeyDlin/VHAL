#pragma once
#include <VHAL.h>


// A display panel behind a bus, as a renderer sees it: its size, the write window, pixel writes (synchronous, or started
// in the background) and the byte order the panel expects. Panel setup (reset, orientation, sleep) is the driver's own
// business and is not part of this interface.
class IDisplay {
public:
	// The format of the bytes given to WritePixelBytes and WritePixelBytesAsync.
	enum class PixelFormat : uint8 {
		// 16 bits per pixel, most significant byte first: the panel's wire order, so the bytes go to the bus without a copy.
		Rgb565BigEndian
	};


	virtual ~IDisplay() = default;

	// Logical size in the current orientation.
	virtual uint16 GetWidth() const = 0;

	virtual uint16 GetHeight() const = 0;

	virtual PixelFormat GetPixelFormat() const = 0;

	// Opens the write window with both ends inclusive; the next pixel writes fill it row by row.
	virtual ResultStatus SetWindow(uint16 x0, uint16 y0, uint16 x1, uint16 y1) = 0;

	virtual ResultStatus WritePixelBytes(const uint8* pixels, uint32 byteCount) = 0;

	// Starts the write in the background. The buffer must stay unchanged until IsWriteDone() is true or WaitWriteDone()
	// returns ok. notSupported when the driver or the bus cannot write in the background; use WritePixelBytes then.
	virtual ResultStatus WritePixelBytesAsync(const uint8* pixels, uint32 byteCount) = 0;

	// True when no background write is in flight. Also true when the driver cannot write in the background.
	virtual bool IsWriteDone() = 0;

	// Waits up to timeoutMs for the background write. ok when it has finished or none was started, timeout otherwise.
	virtual ResultStatus WaitWriteDone(uint32 timeoutMs) = 0;

	// ok when the bus can read the buffer (for example internal RAM only on the ESP32), invalidParameter otherwise.
	virtual ResultStatus CheckDmaBuffer(const uint8* buffer, uint32 size) = 0;
};
