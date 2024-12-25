#pragma once
#include <BSP.h>
#include <Utilities/Math/Filter/IntWindow.h>
#include <Utilities/Math/VMath/Utilities.h>


/*
	// LiIon simple profile
	auto battery = BatteryLevel({
		{4.2, 100},
		{3.8, 90},
		{3.55, 20},
		{3.1, 10},
		{3.0, 0}
	});
*/

class BatteryLevel {
public:
	struct BatteryProfilePoint {
		float voltage;
		uint8 level;
	};


private:
	IntWindow<float> voltageLevel;
	uint8 cells; 		// Number of battery cells
	uint16 divisions; 	// Number of charge levels

	uint16 profileCount;
	BatteryProfilePoint* profile;


public:
	// if jitter = 0, it is calculated automatically
	BatteryLevel(const std::initializer_list<BatteryProfilePoint>& batteryProfile, uint8 divisionsQuantity = 10, uint8 cellsQuantity = 1, float jitter = 0) {
		cells = cellsQuantity;
		divisions = divisionsQuantity;

		profileCount = batteryProfile.size();
		profile = new BatteryProfilePoint[profileCount];
		std::copy(batteryProfile.begin(), batteryProfile.end(), profile);

		if (jitter <= 0) {
			float maxVoltage = profile[0].voltage * cells;
			float minVoltage = profile[profileCount - 1].voltage * cells;
			jitter = ((maxVoltage - minVoltage) / (float)divisions) / 2.0;
		}

		voltageLevel = IntWindow<float>(jitter, IntWindow<>::PullType::noPull);
	}





	// How full is the charge scale
	uint8 GetDivision(float batteryVoltage) {
		float voltage = voltageLevel.Get(batteryVoltage);
		voltage /= (float)cells;


		if (voltage >= profile[0].voltage) {
			return divisions;
		}

		if (voltage <= profile[profileCount - 1].voltage) {
			return 0;
		}


		float level = 0;
		for (int32 i = profileCount - 1; i >= 0; i--) {
			if (voltage < profile[i].voltage) {
				level = VMath::Extrapolation<float>(
					profile[i].voltage, profile[i].level,
					profile[i + 1].voltage, profile[i + 1].level,
					voltage
				);
				break;
			}
		}


		return static_cast<uint8>(level / (100.0 / (float)divisions));
	}
};
