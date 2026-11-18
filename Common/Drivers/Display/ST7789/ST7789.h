#pragma once
#include <VHAL.h>
#include <algorithm>


// ST7789V2 panel on a transmit-only SPI bus with a data/command line and an active-low reset line (both are AGPIO, so
// the polarity comes from the AGPIO inversion flag). The caller configures the bus (SPI mode 0, MSB first, transmit only,
// the panel's maximum clock); this driver only sends bytes. Portable: it uses VHAL adapters and nothing else.
class ST7789 {
public:
	enum class Rotation : uint8 {
		Portrait,
		Landscape,
		PortraitInverted,
		LandscapeInverted
	};


	struct Config {
		// Physical size and offsets in the Portrait orientation. The offsets place the visible area inside the
		// controller memory; the M5StickC PLUS2 panel (135 x 240) needs 52 and 40.
		uint16 width = 135;
		uint16 height = 240;
		uint16 offsetX = 0;
		uint16 offsetY = 0;
		Rotation rotation = Rotation::Portrait;
		bool invertColors = false;
		// Selects BGR instead of RGB subpixel order (MADCTL bit 3).
		bool bgr = false;
	};


private:
	enum class Command : uint8 {
		SoftwareReset = 0x01,
		SleepIn = 0x10,
		SleepOut = 0x11,
		NormalMode = 0x13,
		InversionOff = 0x20,
		InversionOn = 0x21,
		DisplayOn = 0x29,
		ColumnAddressSet = 0x2A,
		RowAddressSet = 0x2B,
		MemoryWrite = 0x2C,
		MemoryAccessControl = 0x36,
		PixelFormat = 0x3A
	};

	// Pixels converted to wire order are staged here, so a transfer never needs a heap buffer.
	static constexpr uint32 chunkPixels = 256;
	// Longest wait for an asynchronous pixel write before a command gives up on the panel.
	static constexpr uint32 writeWaitMs = 1000;

	ASPI& spi;
	AGPIO& dataCommand;
	AGPIO& reset;
	Config config;
	alignas(4) uint8 wireBuffer[chunkPixels * 2] = {};


public:
	ST7789(ASPI& spiBus, AGPIO& dataCommandPin, AGPIO& resetPin, Config panelConfig)
		: spi(spiBus), dataCommand(dataCommandPin), reset(resetPin), config(panelConfig) {
	}


	// Configures the data/command and reset lines as outputs: a line left as an input drives nothing, and the panel then
	// stays in reset while every SPI write still reports success.
	ResultStatus Init() {
		ResultStatus status = dataCommand.SetParameters({ AGPIO::Mode::Output });
		if (status != ResultStatus::ok) {
			return status;
		}

		reset.Reset();
		status = reset.SetParameters({ AGPIO::Mode::Output });
		if (status != ResultStatus::ok) {
			return status;
		}

		Reset();

		status = WriteCommand(Command::SoftwareReset);
		if (status != ResultStatus::ok) {
			return status;
		}
		System::DelayMs(150);

		status = WriteCommand(Command::SleepOut);
		if (status != ResultStatus::ok) {
			return status;
		}
		System::DelayMs(120);

		// 16 bits per pixel, RGB565.
		uint8 pixelFormat[1] = { 0x55 };
		status = SendCommand(Command::PixelFormat, pixelFormat, 1);
		if (status != ResultStatus::ok) {
			return status;
		}

		status = SetRotation(config.rotation);
		if (status != ResultStatus::ok) {
			return status;
		}

		status = WriteCommand(config.invertColors ? Command::InversionOn : Command::InversionOff);
		if (status != ResultStatus::ok) {
			return status;
		}

		status = WriteCommand(Command::NormalMode);
		if (status != ResultStatus::ok) {
			return status;
		}
		System::DelayMs(10);

		status = WriteCommand(Command::DisplayOn);
		if (status != ResultStatus::ok) {
			return status;
		}
		System::DelayMs(10);

		return FillRect(0, 0, GetWidth(), GetHeight(), 0x0000);
	}


	// Changes the orientation. The logical width and height swap for the landscape orientations.
	ResultStatus SetRotation(Rotation value) {
		config.rotation = value;

		uint8 memoryAccess[1] = { static_cast<uint8>(MemoryAccessBits() | (config.bgr ? 0x08 : 0x00)) };
		return SendCommand(Command::MemoryAccessControl, memoryAccess, 1);
	}


	ResultStatus SetInversion(bool enabled) {
		return WriteCommand(enabled ? Command::InversionOn : Command::InversionOff);
	}


	ResultStatus SetSleep(bool sleeping) {
		if (sleeping) {
			return WriteCommand(Command::SleepIn);
		}

		ResultStatus status = WriteCommand(Command::SleepOut);
		if (status == ResultStatus::ok) {
			System::DelayMs(120);
		}
		return status;
	}


	uint16 GetWidth() const {
		return IsLandscape() ? config.height : config.width;
	}


	uint16 GetHeight() const {
		return IsLandscape() ? config.width : config.height;
	}


	// Sets the window in logical coordinates, both ends inclusive, and leaves the controller ready for WritePixels.
	ResultStatus SetWindow(uint16 x0, uint16 y0, uint16 x1, uint16 y1) {
		if (x1 < x0 || y1 < y0 || x1 >= GetWidth() || y1 >= GetHeight()) {
			return ResultStatus::outOfRange;
		}

		uint16 columnStart = static_cast<uint16>(x0 + ColumnOffset());
		uint16 columnEnd = static_cast<uint16>(x1 + ColumnOffset());
		uint16 rowStart = static_cast<uint16>(y0 + RowOffset());
		uint16 rowEnd = static_cast<uint16>(y1 + RowOffset());

		uint8 columns[4] = {
			static_cast<uint8>(columnStart >> 8),
			static_cast<uint8>(columnStart & 0xFF),
			static_cast<uint8>(columnEnd >> 8),
			static_cast<uint8>(columnEnd & 0xFF)
		};
		uint8 rows[4] = {
			static_cast<uint8>(rowStart >> 8),
			static_cast<uint8>(rowStart & 0xFF),
			static_cast<uint8>(rowEnd >> 8),
			static_cast<uint8>(rowEnd & 0xFF)
		};

		ResultStatus status = SendCommand(Command::ColumnAddressSet, columns, 4);
		if (status != ResultStatus::ok) {
			return status;
		}

		status = SendCommand(Command::RowAddressSet, rows, 4);
		if (status != ResultStatus::ok) {
			return status;
		}

		return WriteCommand(Command::MemoryWrite);
	}


	// Streams bytes that are already in panel order (two per pixel, high byte first) after SetWindow, with no copy. The
	// bus splits the buffer into transfers of at most its chunk size, with chip select held low across them.
	ResultStatus WritePixelBytes(const uint8* panelBytes, uint32 byteCount) {
		return SendData(panelBytes, byteCount);
	}


	// Streams pixels after SetWindow, in the order the window is filled (row by row). The values are native RGB565;
	// the byte swap to the MSB-first wire order happens here, so callers never swap. Costs one copy per 256 pixels;
	// use WritePixelBytes with a buffer that already holds the panel order.
	ResultStatus WritePixels(const uint16* rgb565, uint32 count) {
		uint32 offset = 0;
		while (offset < count) {
			uint32 piece = std::min<uint32>(count - offset, chunkPixels);
			for (uint32 index = 0; index < piece; index++) {
				uint16 pixel = rgb565[offset + index];
				wireBuffer[index * 2] = static_cast<uint8>(pixel >> 8);
				wireBuffer[index * 2 + 1] = static_cast<uint8>(pixel & 0xFF);
			}

			ResultStatus status = SendData(wireBuffer, piece * 2);
			if (status != ResultStatus::ok) {
				return status;
			}
			offset = offset + piece;
		}
		return ResultStatus::ok;
	}


	// Starts streaming bytes already in panel order after SetWindow, without waiting for the bus. Returns notSupported
	// when the bus has no asynchronous write; the caller then uses WritePixelBytes. Collect the write with IsWriteDone or
	// WaitWriteDone before the buffer is reused, and before the next SetWindow or other command.
	ResultStatus WritePixelBytesAsync(const uint8* panelBytes, uint32 byteCount) {
		dataCommand.Set();
		return spi.BeginWriteBytes(panelBytes, byteCount);
	}


	// True when no asynchronous pixel write is in flight (also when the bus cannot do one).
	bool IsWriteDone() {
		ResultStatus status = spi.WaitWriteBytes(0);
		return status != ResultStatus::timeout;
	}


	// Waits up to timeoutMs for the asynchronous pixel write. Returns ok when it has finished, or when there was none.
	ResultStatus WaitWriteDone(uint32 timeoutMs) {
		ResultStatus status = spi.WaitWriteBytes(timeoutMs);
		if (status == ResultStatus::notSupported) {
			return ResultStatus::ok;
		}
		return status;
	}


	// Whether a buffer can be used for pixel writes on this bus (see SPIAdapter::CheckDmaBuffer).
	ResultStatus CheckDmaBuffer(const uint8* buffer, uint32 size) {
		return spi.CheckDmaBuffer(buffer, size);
	}


	// Fills the visible part of the rectangle. Coordinates are logical; anything outside the panel is clipped.
	ResultStatus FillRect(uint16 x, uint16 y, uint16 width, uint16 height, uint16 color) {
		if (width == 0 || height == 0 || x >= GetWidth() || y >= GetHeight()) {
			return ResultStatus::ok;
		}

		uint16 visibleWidth = static_cast<uint16>(std::min<uint32>(width, GetWidth() - x));
		uint16 visibleHeight = static_cast<uint16>(std::min<uint32>(height, GetHeight() - y));
		ResultStatus status = SetWindow(x, y, static_cast<uint16>(x + visibleWidth - 1), static_cast<uint16>(y + visibleHeight - 1));
		if (status != ResultStatus::ok) {
			return status;
		}

		FillWireBuffer(color);
		uint32 remaining = static_cast<uint32>(visibleWidth) * visibleHeight;
		while (remaining > 0) {
			uint32 piece = std::min<uint32>(remaining, chunkPixels);
			status = SendData(wireBuffer, piece * 2);
			if (status != ResultStatus::ok) {
				return status;
			}
			remaining = remaining - piece;
		}
		return ResultStatus::ok;
	}


private:
	bool IsLandscape() const {
		return config.rotation == Rotation::Landscape || config.rotation == Rotation::LandscapeInverted;
	}


	// The controller addresses memory in portrait coordinates, so the offsets swap with the orientation.
	uint16 ColumnOffset() const {
		return IsLandscape() ? config.offsetY : config.offsetX;
	}


	uint16 RowOffset() const {
		return IsLandscape() ? config.offsetX : config.offsetY;
	}


	uint8 MemoryAccessBits() const {
		switch (config.rotation) {
			case Rotation::Portrait:
				return 0x00;
			case Rotation::Landscape:
				return 0x60;
			case Rotation::PortraitInverted:
				return 0xC0;
			case Rotation::LandscapeInverted:
				return 0xA0;
		}
		return 0x00;
	}


	void Reset() {
		reset.Set();
		System::DelayMs(20);
		reset.Reset();
		System::DelayMs(120);
	}


	void FillWireBuffer(uint16 color) {
		uint8 highByte = static_cast<uint8>(color >> 8);
		uint8 lowByte = static_cast<uint8>(color & 0xFF);
		for (uint32 index = 0; index < chunkPixels; index++) {
			wireBuffer[index * 2] = highByte;
			wireBuffer[index * 2 + 1] = lowByte;
		}
	}


	// An asynchronous pixel write reads its data while the data/command line is high. Changing the line before it has
	// finished would turn the rest of it into command bytes, so commands and data wait for it first.
	ResultStatus FinishPendingWrite() {
		return WaitWriteDone(writeWaitMs);
	}


	ResultStatus WriteCommand(Command command) {
		ResultStatus status = FinishPendingWrite();
		if (status != ResultStatus::ok) {
			return status;
		}
		dataCommand.Reset();
		return spi.Write<uint8>(static_cast<uint8>(command));
	}


	ResultStatus SendData(const uint8* data, uint32 size) {
		ResultStatus status = FinishPendingWrite();
		if (status != ResultStatus::ok) {
			return status;
		}
		dataCommand.Set();
		return spi.WriteArray<uint8>(data, size);
	}


	ResultStatus SendCommand(Command command, const uint8* data, uint32 size) {
		ResultStatus status = WriteCommand(command);
		if (status != ResultStatus::ok) {
			return status;
		}
		return SendData(data, size);
	}
};
