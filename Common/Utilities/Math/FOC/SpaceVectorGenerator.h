#pragma once
#include <BSP.h>
#include <cmath>



class SpaceVectorGenerator {
private:
	struct {
		float alpha; // reference alpha-axis phase voltage
		float beta;  // reference beta-axis phase voltage
	} in;


public:
	struct Out {
		float phaseA = 0; // reference phase-a switching function
		float phaseB = 0; // reference phase-b switching function
		float phaseC = 0; // reference phase-c switching function
	} out;


	SpaceVectorGenerator& Set(float alpha, float beta) {
		in.alpha = alpha;
		in.beta = beta;

		return *this;
	}
	

	SpaceVectorGenerator& ResolveCommonMode() {
		static float const SQRT3_D2 = (std::sqrt(3.0f) / 2);	
																				
		float Va = v.Ualpha; // Inv Clarke					
		float Vb = -(in.alpha / 2) + (SQRT3_D2 * in.beta);													
		float Vc = -(in.alpha / 2) - (SQRT3_D2 * in.beta);	
		
		float Vmax = std::max(std::max(Va, Vb), Vc);
		float Vmin = std::min(std::min(Va, Vb), Vc);											
																				
		float Vcommon = (Vmax + Vmin) / 2;	
																				
		out.phaseA = Va - Vcommon;														
		out.phaseB = Vb - Vcommon;														
		out.phaseC = Vc - Vcommon;	

		return *this;
	}


	SpaceVectorGenerator& ResolveDiscontinuousPwmMode() {
		static float const SQRT3_D2 = (std::sqrt(3.0f) / 2);

		float Va = in.beta;
		float Vb = (in.beta / 2) + (SQRT3_D2 * in.alpha);
		float Vc = Vb - Va;

		// sector determination
		uint8 sector = 3;														
		sector = Vb > 0 ? sector - 1 : sector;						
		sector = Vc > 0 ? sector - 1 : sector;						
		sector = Va < 0 ? 7 - sector : sector;		


		switch (sector) {
			case 1:
			case 6:
				out.phaseA = 0;
				out.phaseB = Vc;
				out.phaseC = Vb;	
			break;

			case 2:
			case 3:
				out.phaseA = -Vc;
				out.phaseB = 0;
				out.phaseC = Va;
			break;

			default:
				out.phaseA = -Vb;
				out.phaseB = -Va;
				out.phaseC = 0;
			break;
		}

		out.phaseA = (out.phaseA * 2) - 1;
		out.phaseB = (out.phaseB * 2) - 1;
		out.phaseC = (out.phaseC * 2) - 1;

		return *this;
	}


	Out Get() {
		return out;
	}
};
