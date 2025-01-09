#pragma once
#include <BSP.h>


class ITermistor {
public:
	enum class Resolution : uint16 {
		B4 = 15,
		B5 = 31,
		B6 = 63,
		B7 = 127,
		B8 = 255,
		B9 = 511,
		B10 = 1023,
		B11 = 2047,
		B12 = 4095,
		B13 = 8191,
		B14 = 16383,
		B15 = 32767,
		B16 = 65535
	};

	enum class Position { Top, Bottom };

	struct Config {
		Position thermistorPosition = Position::Top;
		float dividerResistor = 10; // kOhm
		float adcReference = 3.3; 	// V
		Resolution adcResolution = Resolution::B12;
	};


private:
	Config config;


public:
	void SetConfig(Config val) {
		config = val;
	}


	float GetTemperature(uint16 adcVal) {
		auto resistance = GetResistance(adcVal);

		auto table = GetTermistorConfig()->table;
		auto tableSize = GetTermistorConfig()->tableSize;


		if(resistance >= table[0][1]) {
			return Extrapolation(
				table[1][1],
				table[1][0],
				table[0][1],
				table[0][0],
				resistance
			);
		}

		if(resistance <= table[tableSize - 1][1]) {
			return Extrapolation(
				table[tableSize - 2][1],
				table[tableSize - 2][0],
				table[tableSize - 1][1],
				table[tableSize - 1][0],
				resistance
			);
		}

		for(uint16 i = 1; i < tableSize; i++) {
			if(resistance > table[i][1]) {
				return Interpolation(
					table[i - 1][1],
					table[i - 1][0],
					table[i][1],
					table[i][0],
					resistance
				);
			}
		}

		return 0;
	}


private:
	static float Extrapolation(float x0, float y0, float x1, float y1, float x2) {
		return y0 + (x2 - x0) * (y1 - y0) / (x1 - x0);
	}
	static float Interpolation(float x0, float y0, float x1, float y1, float x2) {
		return (((x2 - x0) * (y1 - y0)) / (x1 - x0)) + y0;
	}


	float GetResistance(uint16 adcVal) {
		float adcStep = config.adcReference / static_cast<float>(config.adcResolution);

		float termistorVolt = static_cast<float>(adcVal) * adcStep;

		if(config.thermistorPosition == Position::Top) {
			return (config.dividerResistor * (config.adcReference - termistorVolt)) / termistorVolt;
		} else {
			return (config.dividerResistor * termistorVolt) / (config.adcReference - termistorVolt);
		}
	}


protected:
	struct TermistorConfig {
		uint8 r25; // Thermistor resistance at 25 degrees Celsius

		// Correction table
		// This is a two-dimensional float array: { {temperature, resistance (kOhm)}, ... }
		// The first element should start with the highest resistance value, followed by descending order.
		const float (*table)[2];

		uint8 tableSize; // Size of the correction table
	};

	virtual TermistorConfig* GetTermistorConfig() = 0;
};