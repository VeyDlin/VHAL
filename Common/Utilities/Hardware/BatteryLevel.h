#pragma once
#include <System/System.h>
#include <Utilities/Math/Filter/Hysteresis.h>
#include <Utilities/Math/VMath/Utilities.h>


// A class for modeling the state of charge (SoC) of a battery using a custom voltage profile
// * This class provides a mechanism to determine the charge level of a battery based on its voltage
// * using a user-defined voltage-to-level profile
// * It supports multiple battery cells and configurable charge levels
// Example Usage:
/*
	// Define a simple Li-Ion battery profile
	auto battery = BatteryLevel({
	    {4.2, 100},  // Voltage at 100% charge
	    {3.8, 90},   // Voltage at 90% charge
	    {3.55, 20},  // Voltage at 20% charge
	    {3.1, 10},   // Voltage at 10% charge
	    {3.0, 0}     // Voltage at 0% charge
	});
	float voltage = 3.85;
	uint8 division = battery.GetDivision(voltage); // Returns charge level division
*/
class BatteryLevel {
public:
	// Represents a point in the voltage-to-level profile
	struct BatteryProfilePoint {
		float voltage;	// voltage The voltage of the battery
		uint8 level;	// level The corresponding charge level (0-100%)
	};


private:
    Hysteresis<float> voltageLevel;  // Filters battery voltage to reduce noise
    uint8 cells;                     // Number of cells in the battery pack
    uint16 divisions;                // Number of charge level divisions
    uint16 profileCount;             // Number of points in the battery profile
    BatteryProfilePoint* profile;    // Array of battery profile points


public:
    // * batteryProfile		- Initializer list of voltage-to-level profile points
    // * divisionsQuantity	- Number of charge level divisions (default: 10)
    // * cellsQuantity		- Number of battery cells in series (default: 1)
    // * jitter				- Voltage tolerance for hysteresis filtering (default: auto-calculated)
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

		voltageLevel = Hysteresis<float>(jitter, Hysteresis<>::PullType::NoPull);
	}


    // Gets the charge level division based on the battery voltage
    // This method calculates the charge level by mapping the voltage to the user-defined profile
    // It supports extrapolation between profile points
    // * batteryVoltage - The current voltage of the battery
    // > return Charge level division (0 to `divisions`)
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