#pragma once
#include <Adapter/Port/ESP32/Adapter/PPAAdapter.h>


using APPA = class PPAAdapterESP;


class PPAAdapterESP : public PPAAdapter {
public:
	struct FillColorFormat {
		static inline constexpr FillColorFormatOption ARGB8888   { PPA_FILL_COLOR_MODE_ARGB8888 };
		static inline constexpr FillColorFormatOption RGB888     { PPA_FILL_COLOR_MODE_RGB888 };
		static inline constexpr FillColorFormatOption RGB565     { PPA_FILL_COLOR_MODE_RGB565 };
		static inline constexpr FillColorFormatOption YUV422     { PPA_FILL_COLOR_MODE_YUV422_UYVY };
		static inline constexpr FillColorFormatOption GRAY8      { PPA_FILL_COLOR_MODE_GRAY8 };
	};

	struct BlendColorFormat {
		static inline constexpr BlendColorFormatOption ARGB8888  { PPA_BLEND_COLOR_MODE_ARGB8888 };
		static inline constexpr BlendColorFormatOption RGB888    { PPA_BLEND_COLOR_MODE_RGB888 };
		static inline constexpr BlendColorFormatOption RGB565    { PPA_BLEND_COLOR_MODE_RGB565 };
		static inline constexpr BlendColorFormatOption A8        { PPA_BLEND_COLOR_MODE_A8 };
		static inline constexpr BlendColorFormatOption A4        { PPA_BLEND_COLOR_MODE_A4 };
		static inline constexpr BlendColorFormatOption YUV420    { PPA_BLEND_COLOR_MODE_YUV420 };
		static inline constexpr BlendColorFormatOption YUV422    { PPA_BLEND_COLOR_MODE_YUV422_UYVY };
		static inline constexpr BlendColorFormatOption GRAY8     { PPA_BLEND_COLOR_MODE_GRAY8 };
	};

	struct SrmColorFormat {
		static inline constexpr SrmColorFormatOption ARGB8888    { PPA_SRM_COLOR_MODE_ARGB8888 };
		static inline constexpr SrmColorFormatOption RGB888      { PPA_SRM_COLOR_MODE_RGB888 };
		static inline constexpr SrmColorFormatOption RGB565      { PPA_SRM_COLOR_MODE_RGB565 };
		static inline constexpr SrmColorFormatOption YUV420      { PPA_SRM_COLOR_MODE_YUV420 };
		static inline constexpr SrmColorFormatOption YUV444      { PPA_SRM_COLOR_MODE_YUV444 };
		static inline constexpr SrmColorFormatOption YUV422      { PPA_SRM_COLOR_MODE_YUV422_UYVY };
		static inline constexpr SrmColorFormatOption GRAY8       { PPA_SRM_COLOR_MODE_GRAY8 };
	};

	struct BurstLength {
		static inline constexpr BurstLengthOption B8             { PPA_DATA_BURST_LENGTH_8 };
		static inline constexpr BurstLengthOption B16            { PPA_DATA_BURST_LENGTH_16 };
		static inline constexpr BurstLengthOption B32            { PPA_DATA_BURST_LENGTH_32 };
		static inline constexpr BurstLengthOption B64            { PPA_DATA_BURST_LENGTH_64 };
		static inline constexpr BurstLengthOption B128           { PPA_DATA_BURST_LENGTH_128 };
	};


public:
	PPAAdapterESP() = default;

	~PPAAdapterESP() {
		if (fillClient)  ppa_unregister_client(fillClient);
		if (blendClient) ppa_unregister_client(blendClient);
		if (srmClient)   ppa_unregister_client(srmClient);
	}


	// ==================== Fill ====================

	ResultStatus FillOperation(const FillConfig& config) override {
		auto status = EnsureClient(fillClient, PPA_OPERATION_FILL);
		if (status != ResultStatus::ok) {
			return status;
		}

		ppa_fill_oper_config_t cfg = {};
		cfg.out.buffer = config.buffer;
		cfg.out.buffer_size = config.bufferSize;
		cfg.out.pic_w = config.picWidth;
		cfg.out.pic_h = config.picHeight;
		cfg.out.block_offset_x = config.offsetX;
		cfg.out.block_offset_y = config.offsetY;
		cfg.out.fill_cm = static_cast<ppa_fill_color_mode_t>(config.format.Get());

		cfg.fill_block_w = config.fillWidth;
		cfg.fill_block_h = config.fillHeight;
		cfg.fill_argb_color = {
			.val = PackARGB(config.color)
		};

		cfg.mode = PPA_TRANS_MODE_BLOCKING;

		if (ppa_do_fill(fillClient, &cfg) != ESP_OK) {
			return ResultStatus::error;
		}

		return ResultStatus::ok;
	}


	// ==================== Blend ====================

	ResultStatus BlendOperation(const BlendConfig& config) override {
		auto status = EnsureClient(blendClient, PPA_OPERATION_BLEND);
		if (status != ResultStatus::ok) {
			return status;
		}

		ppa_blend_oper_config_t cfg = {};

		// Background
		cfg.in_bg.buffer = config.background.buffer;
		cfg.in_bg.pic_w = config.background.picWidth;
		cfg.in_bg.pic_h = config.background.picHeight;
		cfg.in_bg.block_w = config.background.blockWidth;
		cfg.in_bg.block_h = config.background.blockHeight;
		cfg.in_bg.block_offset_x = config.background.offsetX;
		cfg.in_bg.block_offset_y = config.background.offsetY;
		cfg.in_bg.blend_cm = static_cast<ppa_blend_color_mode_t>(config.bgFormat.Get());

		// Foreground
		cfg.in_fg.buffer = config.foreground.buffer;
		cfg.in_fg.pic_w = config.foreground.picWidth;
		cfg.in_fg.pic_h = config.foreground.picHeight;
		cfg.in_fg.block_w = config.foreground.blockWidth;
		cfg.in_fg.block_h = config.foreground.blockHeight;
		cfg.in_fg.block_offset_x = config.foreground.offsetX;
		cfg.in_fg.block_offset_y = config.foreground.offsetY;
		cfg.in_fg.blend_cm = static_cast<ppa_blend_color_mode_t>(config.fgFormat.Get());

		// Output
		cfg.out.buffer = config.output.buffer;
		cfg.out.buffer_size = config.output.bufferSize;
		cfg.out.pic_w = config.output.picWidth;
		cfg.out.pic_h = config.output.picHeight;
		cfg.out.block_offset_x = config.output.offsetX;
		cfg.out.block_offset_y = config.output.offsetY;
		cfg.out.blend_cm = static_cast<ppa_blend_color_mode_t>(config.outFormat.Get());

		// Alpha
		cfg.bg_alpha_update_mode = CastAlphaMode(config.bgAlphaMode);
		cfg.bg_alpha_fix_val = config.bgAlphaValue;
		cfg.fg_alpha_update_mode = CastAlphaMode(config.fgAlphaMode);
		cfg.fg_alpha_fix_val = config.fgAlphaValue;

		// Foreground fixed color (for A8/A4 modes)
		cfg.fg_fix_rgb_val = { .val = PackRGB(config.fgFixColor) };

		cfg.mode = PPA_TRANS_MODE_BLOCKING;

		if (ppa_do_blend(blendClient, &cfg) != ESP_OK) {
			return ResultStatus::error;
		}

		return ResultStatus::ok;
	}


	// ==================== Scale/Rotate/Mirror ====================

	ResultStatus ScaleRotateMirrorOperation(const SrmConfig& config) override {
		auto status = EnsureClient(srmClient, PPA_OPERATION_SRM);
		if (status != ResultStatus::ok) {
			return status;
		}

		ppa_srm_oper_config_t cfg = {};

		// Input
		cfg.in.buffer = config.input.buffer;
		cfg.in.pic_w = config.input.picWidth;
		cfg.in.pic_h = config.input.picHeight;
		cfg.in.block_w = config.input.blockWidth;
		cfg.in.block_h = config.input.blockHeight;
		cfg.in.block_offset_x = config.input.offsetX;
		cfg.in.block_offset_y = config.input.offsetY;
		cfg.in.srm_cm = static_cast<ppa_srm_color_mode_t>(config.inFormat.Get());

		// Output
		cfg.out.buffer = config.output.buffer;
		cfg.out.buffer_size = config.output.bufferSize;
		cfg.out.pic_w = config.output.picWidth;
		cfg.out.pic_h = config.output.picHeight;
		cfg.out.block_offset_x = config.output.offsetX;
		cfg.out.block_offset_y = config.output.offsetY;
		cfg.out.srm_cm = static_cast<ppa_srm_color_mode_t>(config.outFormat.Get());

		// Transform
		cfg.rotation_angle = CastRotation(config.rotation);
		cfg.scale_x = config.scaleX;
		cfg.scale_y = config.scaleY;
		cfg.mirror_x = config.mirrorX;
		cfg.mirror_y = config.mirrorY;

		// Alpha
		cfg.alpha_update_mode = CastAlphaMode(config.alphaMode);
		cfg.alpha_fix_val = config.alphaValue;

		// Data manipulation
		cfg.rgb_swap = config.rgbSwap;
		cfg.byte_swap = config.byteSwap;

		cfg.mode = PPA_TRANS_MODE_BLOCKING;

		if (ppa_do_scale_rotate_mirror(srmClient, &cfg) != ESP_OK) {
			return ResultStatus::error;
		}

		return ResultStatus::ok;
	}


protected:
	ppa_client_handle_t fillClient = nullptr;
	ppa_client_handle_t blendClient = nullptr;
	ppa_client_handle_t srmClient = nullptr;


	ResultStatus Initialization() override {
		auto status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		if (fillClient)  { ppa_unregister_client(fillClient);  fillClient = nullptr; }
		if (blendClient) { ppa_unregister_client(blendClient); blendClient = nullptr; }
		if (srmClient)   { ppa_unregister_client(srmClient);   srmClient = nullptr; }

		return AfterInitialization();
	}


private:
	ResultStatus EnsureClient(ppa_client_handle_t& client, ppa_operation_t operType) {
		if (client) {
			return ResultStatus::ok;
		}

		ppa_client_config_t cfg = {};
		cfg.oper_type = operType;
		cfg.max_pending_trans_num = 1;

		if (ppa_register_client(&cfg, &client) != ESP_OK) {
			return ResultStatus::error;
		}

		return ResultStatus::ok;
	}


	static uint32 PackARGB(const Color& c) {
		return (static_cast<uint32>(c.a) << 24) |
			   (static_cast<uint32>(c.r) << 16) |
			   (static_cast<uint32>(c.g) << 8)  |
			   static_cast<uint32>(c.b);
	}

	static uint32 PackRGB(const Color& c) {
		return (static_cast<uint32>(c.r) << 16) |
			   (static_cast<uint32>(c.g) << 8)  |
			   static_cast<uint32>(c.b);
	}


	static ppa_alpha_update_mode_t CastAlphaMode(AlphaMode mode) {
		switch (mode) {
			case AlphaMode::NoChange: return PPA_ALPHA_NO_CHANGE;
			case AlphaMode::FixValue: return PPA_ALPHA_FIX_VALUE;
			case AlphaMode::Scale:    return PPA_ALPHA_SCALE;
			case AlphaMode::Invert:   return PPA_ALPHA_INVERT;
		}
		return PPA_ALPHA_NO_CHANGE;
	}

	static ppa_srm_rotation_angle_t CastRotation(Rotation rot) {
		switch (rot) {
			case Rotation::Degree0:   return PPA_SRM_ROTATION_ANGLE_0;
			case Rotation::Degree90:  return PPA_SRM_ROTATION_ANGLE_90;
			case Rotation::Degree180: return PPA_SRM_ROTATION_ANGLE_180;
			case Rotation::Degree270: return PPA_SRM_ROTATION_ANGLE_270;
		}
		return PPA_SRM_ROTATION_ANGLE_0;
	}
};
