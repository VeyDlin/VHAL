#pragma once
#include "Types.h"


class SpaceVectorGenerator {
private:
	struct {
		float alpha;		// reference alpha-axis phase voltage
		float beta;			// reference beta-axis phase voltage

		float gain; 		// reference gain voltage 
		float offset;		// reference offset voltage
		float frequency;	// reference frequency
	} in;


public:
	struct In {
		float alpha;		// reference alpha-axis phase voltage
		float beta;			// reference beta-axis phase voltage
	} in;

	struct In {
		float alpha;		// reference alpha-axis phase voltage
		float beta;			// reference beta-axis phase voltage

		float gain; 		// reference gain voltage 
		float offset;		// reference offset voltage
		float frequency;	// reference frequency
	};


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


	// TODO: Ref: svgen.h
	// This module calculates the appropriate duty ratios needed to generate a given
	// stator reference voltage using space vector PWM technique.
	// The stator reference voltage is described by it’s (alpha, beta) components.
	SpaceVectorGenerator& Resolve() {
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
			case 4:
				out.phaseA = Vb;
				out.phaseB = Va - Vc;
				out.phaseC = -Vb;
			break;

			case 2:
			case 5:
				out.phaseA = Vc + Vb;
				out.phaseB = Va;
				out.phaseC = -Va;
			break;

			default:
				out.phaseA = Vc;
				out.phaseB = -Vc;
				out.phaseC = -(Va + Vb);
			break;
		}

		return *this;
	}

	
	// TODO: Ref: svgen_comm.h
	// This module calculates the appropriate duty ratios needed to generate a given
	// stator reference voltage using common mode voltage.The stator reference
	// voltage is described by it’s (alpha, beta) components. 
	// Note that the input range for this particular macro is ± 2 / sqrt(3).
	SpaceVectorGenerator& ResolveCommonMode() {
		static float const SQRT3_D2 = (std::sqrt(3.0f) / 2);	
																				
		float Va = in.alpha;
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


	// TODO: Ref: svgen_dpwm.h
	// This module calculates the appropriate duty ratios needed to generate a given
	// stator reference voltage using space vector PWM technique.The stator
	// reference voltage is described by it’s (alpha, beta) components.
	// Different than the regular SVGEN, this modulation technique keeps one of the
	// three switches off during the entire 1200 to minimize switching losses.
	// This technique is also known as DPWMmin in the literature
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


	// TODO: Ref: svgen_mf.h
	// This module calculates the appropriate duty ratios needed to generate a given
	// stator reference voltage using space vector PWM technique.
	// The stator reference voltage is described by its magnitudeand frequency
	SpaceVectorGenerator& ResolveMagnitudeFrequencyMode() {

		/* Normalise the freq input to appropriate step angle	*/
			/* Here, 1 pu. = 60 degree			*/
		v.StepAngle = _IQmpy(v.Freq, v.FreqMax);
		/* Calculate new angle alpha			*/
		v.EntryOld = v.NewEntry;
		v.Alpha = v.Alpha + v.StepAngle;
		if (v.Alpha >= _IQ(1.0))
			v.Alpha = v.Alpha - _IQ(1.0);
		v.NewEntry = v.Alpha;
		v.dy = _IQsin(_IQmpy(v.NewEntry, PI_THIRD));              /* v.dy = sin(NewEntry)	*/
		v.dx = _IQsin(PI_THIRD - _IQmpy(v.NewEntry, PI_THIRD));     /* v.dx = sin(60-NewEntry)	*/
	/* Determine which sector			*/
		if (v.NewEntry - v.EntryOld < 0) {
			if (v.SectorPointer == 5)
				v.SectorPointer = 0;
			else
				v.SectorPointer = v.SectorPointer + 1;
		}
		if (v.SectorPointer == 0)  /* Sector 1 calculations - a,b,c -. a,b,c*/
		{
			v.Ta = (_IQ(1.0) - v.dx - v.dy) >> 1;
			v.Tb = v.Ta + v.dx;
			v.Tc = _IQ(1.0) - v.Ta;
		} else if (v.SectorPointer == 1)  /* Sector 2 calculations - a,b,c -. b,a,c  &  v.dx <-. v.dy*/
		{
			v.Tb = (_IQ(1.0) - v.dx - v.dy) >> 1;
			v.Ta = v.Tb + v.dy;
			v.Tc = _IQ(1.0) - v.Tb;
		} else if (v.SectorPointer == 2)  /* Sector 3 calculations - a,b,c -. b,c,a		*/
		{
			v.Tb = (_IQ(1.0) - v.dx - v.dy) >> 1;
			v.Tc = v.Tb + v.dx;
			v.Ta = _IQ(1.0) - v.Tb;
		} else if (v.SectorPointer == 3)  /* Sector 4 calculations - a,b,c -. c,b,a  &  v.dx <-. v.dy*/
		{
			v.Tc = (_IQ(1.0) - v.dx - v.dy) >> 1;
			v.Tb = v.Tc + v.dy;
			v.Ta = _IQ(1.0) - v.Tc;
		} else if (v.SectorPointer == 4)  /* Sector 5 calculations - a,b,c -. c,a,b		*/
		{
			v.Tc = (_IQ(1.0) - v.dx - v.dy) >> 1;
			v.Ta = v.Tc + v.dx;
			v.Tb = _IQ(1.0) - v.Tc;
		} else if (v.SectorPointer == 5)  /* Sector 6 calculations - a,b,c -. a,c,b  &  v.dx <-. v.dy*/
		{
			v.Ta = (_IQ(1.0) - v.dx - v.dy) >> 1;
			v.Tc = v.Ta + v.dy;
			v.Tb = _IQ(1.0) - v.Ta;
		}
		/* Convert the unsigned GLOBAL_Q format (ranged (0,1)) . signed GLOBAL_Q format (ranged (-1,1))	*/
		/* Then, multiply with a gain and add an offset.						*/
		v.Ta = (v.Ta - _IQ(0.5)) << 1;
		v.Ta = _IQmpy(v.Gain, v.Ta) + v.Offset;

		v.Tb = (v.Tb - _IQ(0.5)) << 1;
		v.Tb = _IQmpy(v.Gain, v.Tb) + v.Offset;

		v.Tc = (v.Tc - _IQ(0.5)) << 1;
		v.Tc = _IQmpy(v.Gain, v.Tc) + v.Offset;

		return *this;
	}

	
	Out Get() {
		return out;
	}
};
