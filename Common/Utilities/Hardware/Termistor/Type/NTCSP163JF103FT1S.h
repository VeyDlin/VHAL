#pragma once
#include "../ITermistor.h"


class Termistor_NTCSP163JF103FT1S : public ITermistor {
protected:
	const float resistanceTable[191][2] = {
		{-40, 188.5},
		{-39, 178.6},
		{-38, 169.2},
		{-37, 160.4},
		{-36, 152.1},
		{-35, 144.3},
		{-34, 136.9},
		{-33, 130},
		{-32, 123.4},
		{-31, 117.2},
		{-30, 111.3},
		{-29, 105.8},
		{-28, 100.6},
		{-27, 95.64},
		{-26, 90.97},
		{-25, 86.56},
		{-24, 82.38},
		{-23, 78.43},
		{-22, 74.69},
		{-21, 71.14},
		{-20, 67.79},
		{-19, 64.61},
		{-18, 61.6},
		{-17, 58.74},
		{-16, 56.03},
		{-15, 53.46},
		{-14, 51.03},
		{-13, 48.71},
		{-12, 46.52},
		{-11, 44.43},
		{-10, 42.45},
		{-9, 40.57},
		{-8, 38.78},
		{-7, 37.08},
		{-6, 35.46},
		{-5, 33.93},
		{-4, 32.46},
		{-3, 31.07},
		{-2, 29.75},
		{-1, 28.49},
		{0, 27.28},
		{1, 26.14},
		{2, 25.05},
		{3, 24.01},
		{4, 23.02},
		{5, 22.07},
		{6, 21.17},
		{7, 20.31},
		{8, 19.49},
		{9, 18.71},
		{10, 17.96},
		{11, 17.25},
		{12, 16.57},
		{13, 15.91},
		{14, 15.29},
		{15, 14.7 },
		{16, 14.13},
		{17, 13.59},
		{18, 13.07},
		{19, 12.57},
		{20, 12.09},
		{21, 11.64},
		{22, 11.2},
		{23, 10.78},
		{24, 10.38},
		{25, 10},
		{26, 9.633},
		{27, 9.282},
		{28, 8.945},
		{29, 8.622},
		{30, 8.312},
		{31, 8.015},
		{32, 7.73},
		{33, 7.456},
		{34, 7.194},
		{35, 6.942},
		{36, 6.7},
		{37, 6.468},
		{38, 6.245},
		{39, 6.031},
		{40, 5.826},
		{41, 5.628},
		{42, 5.438},
		{43, 5.255},
		{44, 5.08 },
		{45, 4.911},
		{46, 4.749},
		{47, 4.592},
		{48, 4.442},
		{49, 4.297},
		{50, 4.158},
		{51, 4.024},
		{52, 3.895},
		{53, 3.771},
		{54, 3.651},
		{55, 3.536},
		{56, 3.425},
		{57, 3.318},
		{58, 3.215},
		{59, 3.115},
		{60, 3.019},
		{61, 2.927},
		{62, 2.837},
		{63, 2.751},
		{64, 2.668},
		{65, 2.588},
		{66, 2.511},
		{67, 2.436},
		{68, 2.364},
		{69, 2.295},
		{70, 2.227},
		{71, 2.163},
		{72, 2.1},
		{73, 2.039},
		{74, 1.981},
		{75, 1.924},
		{76, 1.869},
		{77, 1.817},
		{78, 1.765},
		{79, 1.716},
		{80, 1.668},
		{81, 1.622},
		{82, 1.577},
		{83, 1.534},
		{84, 1.492},
		{85, 1.451},
		{86, 1.412},
		{87, 1.374},
		{88, 1.337},
		{89, 1.302},
		{90, 1.267},
		{91, 1.234},
		{92, 1.201},
		{93, 1.17 },
		{94, 1.139},
		{95, 1.11 },
		{96, 1.081},
		{97, 1.053},
		{98, 1.027},
		{99, 1.001},
		{100, 0.975},
		{101, 0.951},
		{102, 0.927},
		{103, 0.904},
		{104, 0.881},
		{105, 0.86},
		{106, 0.838},
		{107, 0.818},
		{108, 0.798},
		{109, 0.779},
		{110, 0.76},
		{111, 0.742},
		{112, 0.724},
		{113, 0.707},
		{114, 0.69},
		{115, 0.674},
		{116, 0.658},
		{117, 0.643},
		{118, 0.628},
		{119, 0.613},
		{120, 0.599},
		{121, 0.585},
		{122, 0.572},
		{123, 0.559},
		{124, 0.546},
		{125, 0.534},
		{126, 0.522},
		{127, 0.511},
		{128, 0.499},
		{129, 0.488},
		{130, 0.478},
		{131, 0.467},
		{132, 0.457},
		{133, 0.447},
		{134, 0.437},
		{135, 0.428},
		{136, 0.419},
		{137, 0.41},
		{138, 0.401},
		{139, 0.393},
		{140, 0.385},
		{141, 0.376},
		{142, 0.369},
		{143, 0.361},
		{144, 0.354},
		{145, 0.346},
		{146, 0.339},
		{147, 0.332},
		{148, 0.326},
		{149, 0.319},
		{150, 0.313}
	};

	TermistorConfig termistorConfig;

	virtual TermistorConfig* GetTermistorConfig() override {
		return &termistorConfig;
	}

public:
	Termistor_NTCSP163JF103FT1S() {
		termistorConfig.r25 = 10;
		termistorConfig.table = resistanceTable;
		termistorConfig.tableSize = 191;
	}
};