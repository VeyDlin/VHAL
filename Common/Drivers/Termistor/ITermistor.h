#pragma once
#include <VHAL.h>
#include <Utilities/Math/IQMath/IQ.h>


template <RealType Type = float>
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
		Type dividerResistor = Type(10); // kOhm
		Type adcReference = Type(3.3f); 	// V
		Resolution adcResolution = Resolution::B12;
	};


private:
	Config config;


public:
	void SetConfig(Config val) {
		config = val;
	}


	Type GetTemperature(uint16 adcVal) {
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

		return Type(0);
	}


private:
	static Type Extrapolation(Type x0, Type y0, Type x1, Type y1, Type x2) {
		return y0 + (x2 - x0) * (y1 - y0) / (x1 - x0);
	}
	static Type Interpolation(Type x0, Type y0, Type x1, Type y1, Type x2) {
		return (((x2 - x0) * (y1 - y0)) / (x1 - x0)) + y0;
	}


	Type GetResistance(uint16 adcVal) {
		Type adcStep = config.adcReference / Type(static_cast<int>(config.adcResolution));

		Type termistorVolt = Type(static_cast<int>(adcVal)) * adcStep;

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
		// This is a two-dimensional array: { {temperature, resistance (kOhm)}, ... }
		// The first element should start with the highest resistance value, followed by descending order.
		const Type (*table)[2];

		uint8 tableSize; // Size of the correction table
	};

	virtual TermistorConfig* GetTermistorConfig() = 0;
};
