#pragma once
#include <Adapter/IAdapter.h>

#define VHAL_PPA_ADAPTER


class PPAAdapter : public IAdapter {
public:
	struct Color {
		uint8 r = 0;
		uint8 g = 0;
		uint8 b = 0;
		uint8 a = 0xFF;
	};

	enum class Rotation { Degree0, Degree90, Degree180, Degree270 };

	enum class AlphaMode { NoChange, FixValue, Scale, Invert };

	struct SrmColorFormatOption : IOption<uint32> { using IOption::IOption; };
	struct BlendColorFormatOption : IOption<uint32> { using IOption::IOption; };
	struct FillColorFormatOption : IOption<uint32> { using IOption::IOption; };
	struct BurstLengthOption : IOption<uint32> { using IOption::IOption; };

	struct PictureBlock {
		void* buffer = nullptr;
		uint32 bufferSize = 0;
		uint16 picWidth = 0;
		uint16 picHeight = 0;
		uint16 blockWidth = 0;
		uint16 blockHeight = 0;
		uint16 offsetX = 0;
		uint16 offsetY = 0;
	};

	struct FillConfig {
		void* buffer = nullptr;
		uint32 bufferSize = 0;
		uint16 picWidth = 0;
		uint16 picHeight = 0;
		uint16 offsetX = 0;
		uint16 offsetY = 0;
		uint16 fillWidth = 0;
		uint16 fillHeight = 0;
		Color color;
		FillColorFormatOption format;
	};

	struct BlendConfig {
		PictureBlock background;
		BlendColorFormatOption bgFormat;

		PictureBlock foreground;
		BlendColorFormatOption fgFormat;

		PictureBlock output;
		BlendColorFormatOption outFormat;

		AlphaMode bgAlphaMode = AlphaMode::NoChange;
		uint32 bgAlphaValue = 0;

		AlphaMode fgAlphaMode = AlphaMode::NoChange;
		uint32 fgAlphaValue = 0;

		Color fgFixColor;
	};

	struct SrmConfig {
		PictureBlock input;
		SrmColorFormatOption inFormat;

		PictureBlock output;
		SrmColorFormatOption outFormat;

		Rotation rotation = Rotation::Degree0;
		float scaleX = 1.0f;
		float scaleY = 1.0f;
		bool mirrorX = false;
		bool mirrorY = false;

		AlphaMode alphaMode = AlphaMode::NoChange;
		uint32 alphaValue = 0;

		bool rgbSwap = false;
		bool byteSwap = false;
	};


public:
	PPAAdapter() = default;


	template<typename T>
	inline ResultStatus Fill(T* buffer, uint32 bufferSize, uint16 width, uint16 height, Color color, FillColorFormatOption format) {
		return FillOperation({
			.buffer = static_cast<void*>(buffer),
			.bufferSize = bufferSize,
			.picWidth = width,
			.picHeight = height,
			.offsetX = 0,
			.offsetY = 0,
			.fillWidth = width,
			.fillHeight = height,
			.color = color,
			.format = format,
		});
	}

	inline ResultStatus Fill(const FillConfig& config) {
		return FillOperation(config);
	}

	inline ResultStatus Blend(const BlendConfig& config) {
		return BlendOperation(config);
	}

	inline ResultStatus ScaleRotateMirror(const SrmConfig& config) {
		return ScaleRotateMirrorOperation(config);
	}


protected:
	virtual ResultStatus FillOperation(const FillConfig& config) = 0;
	virtual ResultStatus BlendOperation(const BlendConfig& config) = 0;
	virtual ResultStatus ScaleRotateMirrorOperation(const SrmConfig& config) = 0;
};
