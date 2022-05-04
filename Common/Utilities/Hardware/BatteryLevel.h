#pragma once
#include <BSP.h>
#include <Utilities/Math/Filter/IntWindow.h>



class BatteryLevel {
private:
	IntWindow<float> voltageLevel;
	float minVoltage;
	float division;
	uint16 divisionsQuantity;

public:

	struct BatteryType {
		// Минимальное напряжение ячейки
		float minVoltage;

		// Максимальное напряжение ячейки
		float maxVoltage;
	};


	struct Config {
		BatteryType batteryType;

		// Количество батарейных ячеек
		uint8 cellsQuantity;

		// Количество делений в индикации
		uint16 divisionsQuantity;
	};


public:
	// drawdown - напряжение, на которое может просадится АКБ, но которое не будет учитывать в индикации класс
	// если drawdown = 0 то оно расчитывается автоматически
	BatteryLevel(Config config, float drawdown = 0) {
		divisionsQuantity = config.divisionsQuantity;

		float maxVoltage = config.batteryType.maxVoltage * config.cellsQuantity;
		minVoltage = config.batteryType.minVoltage * config.cellsQuantity;

		float delta = maxVoltage - minVoltage;
		division = delta / config.divisionsQuantity;

		if(drawdown <= 0) {
			drawdown = division / 2.0;
		}
		voltageLevel = IntWindow<float>(drawdown, IntWindow<>::PullType::noPull);
	}


	// На сколько заполнена шкала заряда
	uint8 GetDivision(float currentBatteryVoltage) {
		volatile float voltage = voltageLevel.Get(currentBatteryVoltage);
		volatile float voltageDivision = (voltage - minVoltage) / division;

		if(voltageDivision >= divisionsQuantity) {
			return divisionsQuantity;
		}
		if(voltageDivision < 0) {
			return 0;
		}

		return (uint8)voltageDivision;
	}

};
