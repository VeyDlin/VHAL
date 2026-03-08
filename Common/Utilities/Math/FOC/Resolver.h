#pragma once
#include <VHAL.h>


class Resolver {
private:
	struct {
		int32 rawTheta = 0; // Raw position data from resolver
	} in;


public:
	struct {
		uint16 stepsPerTurn = 0;   // Number of discrete positions
		uint32 mechScaler = 0;     // 0.9999/total count (Q30)
		uint16 polePairs = 2;      // Number of pole pairs
		int32 initTheta = 0;       // Raw angular offset between encoder index and phase a
	} parameters;

	struct Out {
		int32 elecTheta = 0; // Motor Electrical angle (Q24)
		int32 mechTheta = 0; // Motor Mechanical Angle (Q24)
	} out;


	Resolver& Set(int32 rawTheta) {
		in.rawTheta = rawTheta;
		return *this;
	}


	Resolver& Resolve() {
		// Compute the mechanical angle in Q24
		out.mechTheta = in.rawTheta - parameters.initTheta;
		out.mechTheta = ((int64)parameters.mechScaler * (int16)out.mechTheta) >> 1; // Q15 = Q30*Q0
		out.mechTheta &= 0x7FFF;       // Wrap around
		out.mechTheta <<= 9;           // Q15 -> Q24

		// Compute the electrical angle in Q24
		out.elecTheta = parameters.polePairs * out.mechTheta;
		out.elecTheta &= 0x00FFFFFF;   // Wrap around

		return *this;
	}


	Out Get() {
		return out;
	}
};
