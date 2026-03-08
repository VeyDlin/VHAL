#pragma once
#include <cmath>
#include "URgb.h"


namespace Colors {

	struct GammaProfile {
		float gammaR = 2.2f;
		float gammaG = 2.2f;
		float gammaB = 2.2f;

		// Per-channel brightness scale (0.0–1.0)
		// Compensates for different luminous efficacy of R/G/B LEDs
		float scaleR = 1.0f;
		float scaleG = 1.0f;
		float scaleB = 1.0f;

		static constexpr GammaProfile sRGB() { return {2.2f, 2.2f, 2.2f, 1.0f, 1.0f, 1.0f}; }
		static constexpr GammaProfile Linear() { return {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f}; }

		// Apply gamma + brightness scaling: linear color → PWM output
		FRgb Apply(const FRgb& color) const {
			return {
				std::pow(color.r, gammaR) * scaleR,
				std::pow(color.g, gammaG) * scaleG,
				std::pow(color.b, gammaB) * scaleB
			};
		}

		URgb Apply(const URgb& color) const {
			return URgb(Apply(FRgb(color)));
		}

		// Inverse gamma: PWM value → linear color
		FRgb Inverse(const FRgb& color) const {
			return {
				(scaleR > 0) ? std::pow(color.r / scaleR, 1.0f / gammaR) : 0.0f,
				(scaleG > 0) ? std::pow(color.g / scaleG, 1.0f / gammaG) : 0.0f,
				(scaleB > 0) ? std::pow(color.b / scaleB, 1.0f / gammaB) : 0.0f
			};
		}

		FRgb Inverse(const URgb& color) const {
			return Inverse(FRgb(color));
		}
	};

}
