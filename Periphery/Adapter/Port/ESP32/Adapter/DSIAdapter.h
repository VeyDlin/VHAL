#pragma once
#include <Adapter/IAdapter.h>

#define VHAL_DSI_ADAPTER


class DSIAdapter : public IAdapter {
public:
	enum class Mode { Video, Command };
	enum class Polarity { Low, High };

	enum class Irq { TransferDone, RefreshDone };
	enum class Error { None, Transmission, Protocol, Overflow };

	struct ClockSourceOption : IOption<uint32> { using IOption::IOption; };
	struct ColorFormatOption : IOption<uint32> { using IOption::IOption; };
	struct DataEndianOption : IOption<uint32> { using IOption::IOption; };
	struct ColorSpaceOption : IOption<uint32> { using IOption::IOption; };
	struct ColorRangeOption : IOption<uint32> { using IOption::IOption; };
	struct YuvSampleOption : IOption<uint32> { using IOption::IOption; };
	struct YuvConvStdOption : IOption<uint32> { using IOption::IOption; };
	struct YuvPackOrderOption : IOption<uint32> { using IOption::IOption; };
	struct FrameBufferCountOption : IOption<uint8> { using IOption::IOption; };

	struct VideoTiming {
		uint16 hsyncWidth = 0;
		uint16 hBackPorch = 0;
		uint16 hFrontPorch = 0;
		uint16 activeWidth = 0;
		uint16 vsyncWidth = 0;
		uint16 vBackPorch = 0;
		uint16 vFrontPorch = 0;
		uint16 activeHeight = 0;
	};

	struct Parameters {
		Mode mode = Mode::Video;
		uint8 laneCount = 2;
		uint32 laneBitRateMbps = 500;
		uint8 virtualChannel = 0;
		ColorFormatOption colorFormat;
		VideoTiming timing;
		uint32 pixelClockKHz = 0;
		ClockSourceOption clockSource;
		Polarity hsPolarity = Polarity::Low;
		Polarity vsPolarity = Polarity::Low;
		Polarity dePolarity = Polarity::Low;
		FrameBufferCountOption frameBufferCount;
	};


public:
	std::function<void(Irq irqType)> onInterrupt;
	std::function<void(Error errorType)> onError;


protected:
	esp_lcd_dsi_bus_handle_t bus = nullptr;
	esp_lcd_panel_io_handle_t io = nullptr;
	esp_lcd_panel_handle_t panel = nullptr;
	void *frameBuffers[3] = {};
	Parameters parameters;


public:
	DSIAdapter() = default;


	virtual ResultStatus SetParameters(Parameters val) {
		parameters = val;
		return Initialization();
	}

	const Parameters& GetParameters() const {
		return parameters;
	}


	// DCS write commands
	inline ResultStatus WriteCommand(uint8 cmd) {
		return WriteCommandBytes(cmd, nullptr, 0);
	}

	template <typename DataType>
	inline ResultStatus WriteCommand(uint8 cmd, DataType data) {
		return WriteCommandBytes(cmd, reinterpret_cast<const uint8*>(&data), sizeof(DataType));
	}

	template <typename DataType>
	inline ResultStatus WriteCommandArray(uint8 cmd, const DataType *buffer, uint32 size) {
		return WriteCommandBytes(cmd, reinterpret_cast<const uint8*>(buffer), sizeof(DataType) * size);
	}

	inline ResultStatus WriteCommand(uint8 cmd, std::initializer_list<uint8> params) {
		return WriteCommandBytes(cmd, params.begin(), params.size());
	}

	// DCS read commands
	template <typename DataType>
	inline Result<DataType> ReadCommand(uint8 cmd) {
		DataType data;
		return Result<DataType>::Capture(
			ReadCommandBytes(cmd, reinterpret_cast<uint8*>(&data), sizeof(DataType)), data
		);
	}

	template <typename DataType>
	inline ResultStatus ReadCommandArray(uint8 cmd, DataType *buffer, uint32 size) {
		return ReadCommandBytes(cmd, reinterpret_cast<uint8*>(buffer), sizeof(DataType) * size);
	}


	// Control
	virtual ResultStatus Start() = 0;
	virtual ResultStatus Stop() = 0;

	// Framebuffer
	virtual void* GetFrameBuffer(uint8 index = 0) = 0;
	virtual ResultStatus DrawBitmap(int xStart, int yStart, int xEnd, int yEnd, const void *colorData) = 0;

	// Panel operations
	virtual ResultStatus Mirror(bool mirrorX, bool mirrorY) = 0;
	virtual ResultStatus SwapXY(bool swap) = 0;
	virtual ResultStatus SetGap(int xGap, int yGap) = 0;
	virtual ResultStatus InvertColor(bool invert) = 0;
	virtual ResultStatus DisplayOnOff(bool on) = 0;
	virtual ResultStatus DisplaySleep(bool sleep) = 0;

	virtual void IrqHandler() = 0;


protected:
	virtual ResultStatus WriteCommandBytes(uint8 cmd, const uint8 *params, uint32 paramSize) = 0;
	virtual ResultStatus ReadCommandBytes(uint8 cmd, uint8 *data, uint32 size) = 0;

	inline void CallInterrupt(Irq irqType) {
		if (onInterrupt != nullptr) {
			onInterrupt(irqType);
		}
	}

	inline void CallError(Error error) {
		if (onError != nullptr) {
			onError(error);
		}
	}
};
