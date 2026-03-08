#pragma once
#include "../ITermistor.h"


template <RealType Type = float>
class Termistor_B57541G1103F000 : public ITermistor<Type> {
protected:
	const Type resistanceTable[43][2] = {
		{-55.0f, 531.04f},
		{-50.0f, 393.18f},
		{-45.0f, 293.25f},
		{-40.0f, 220.3f},
		{-35.0f, 166.66f},
		{-30.0f, 126.96f},
		{-25.0f, 97.251f},
		{-20.0f, 75.171f},
		{-15.0f, 58.353f},
		{-10.0f, 45.686f},
		{-5.0f, 36.05f},
		{0.0f, 28.665f},
		{5.0f, 22.907f},
		{10.0f, 18.438f},
		{15.0f, 14.92f},
		{20.0f, 12.154f},
		{25.0f, 10.000f},
		{30.0f, 8.2976f},
		{35.0f, 6.8635f},
		{40.0f, 5.7103f},
		{45.0f, 4.8015f},
		{50.0f, 4.0545f},
		{55.0f, 3.417f},
		{60.0f, 2.8952f},
		{65.0f, 2.4714f},
		{70.0f, 2.1183f},
		{75.0f, 1.8194f},
		{80.0f, 1.568f},
		{85.0f, 1.3592f},
		{90.0f, 1.1822f},
		{95.0f, 1.034f},
		{100.0f, 0.90741f},
		{105.0f, 0.79642f},
		{110.0f, 0.70102f},
		{115.0f, 0.61889f},
		{120.0f, 0.54785f},
		{125.0f, 0.48706f},
		{130.0f, 0.43415f},
		{135.0f, 0.38722f},
		{140.0f, 0.34615f},
		{145.0f, 0.31048f},
		{150.0f, 0.2791f},
		{155.0f, 0.25193f}
	};

	typename ITermistor<Type>::TermistorConfig termistorConfig;

	virtual typename ITermistor<Type>::TermistorConfig* GetTermistorConfig() override {
		return &termistorConfig;
	}


public:
	Termistor_B57541G1103F000() {
		termistorConfig.r25 = 10;
		termistorConfig.table = resistanceTable;
		termistorConfig.tableSize = 43;
	}
};
