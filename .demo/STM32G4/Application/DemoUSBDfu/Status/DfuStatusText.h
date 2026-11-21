#pragma once
#include <Drivers/Interface/USB/Class/DFU/USBDfuProtocol.h>
#include <Drivers/Interface/USB/USBProtocol.h>
#include <Utilities/Console/Print.h>


// The names the status line prints. They follow the DFU 1.1 names, so a line can be read against the specification.
namespace DemoUSBDfu {
	inline const char* DfuStateName(DfuState state) {
		switch (state) {
			case DfuState::appIDLE: {
				return "appIDLE";
			}

			case DfuState::appDETACH: {
				return "appDETACH";
			}

			case DfuState::dfuIDLE: {
				return "dfuIDLE";
			}

			case DfuState::dfuDNLOAD_SYNC: {
				return "dfuDNLOAD-SYNC";
			}

			case DfuState::dfuDNBUSY: {
				return "dfuDNBUSY";
			}

			case DfuState::dfuDNLOAD_IDLE: {
				return "dfuDNLOAD-IDLE";
			}

			case DfuState::dfuMANIFEST_SYNC: {
				return "dfuMANIFEST-SYNC";
			}

			case DfuState::dfuMANIFEST: {
				return "dfuMANIFEST";
			}

			case DfuState::dfuMANIFEST_WAIT_RESET: {
				return "dfuMANIFEST-WAIT-RESET";
			}

			case DfuState::dfuUPLOAD_IDLE: {
				return "dfuUPLOAD-IDLE";
			}

			case DfuState::dfuERROR: {
				return "dfuERROR";
			}
		}

		return "Unknown";
	}


	inline const char* DfuStatusName(DfuStatus status) {
		switch (status) {
			case DfuStatus::OK: {
				return "OK";
			}

			case DfuStatus::errTARGET: {
				return "errTARGET";
			}

			case DfuStatus::errFILE: {
				return "errFILE";
			}

			case DfuStatus::errWRITE: {
				return "errWRITE";
			}

			case DfuStatus::errERASE: {
				return "errERASE";
			}

			case DfuStatus::errCHECK_ERASED: {
				return "errCHECK_ERASED";
			}

			case DfuStatus::errPROG: {
				return "errPROG";
			}

			case DfuStatus::errVERIFY: {
				return "errVERIFY";
			}

			case DfuStatus::errADDRESS: {
				return "errADDRESS";
			}

			case DfuStatus::errNOTDONE: {
				return "errNOTDONE";
			}

			case DfuStatus::errFIRMWARE: {
				return "errFIRMWARE";
			}

			case DfuStatus::errVENDOR: {
				return "errVENDOR";
			}

			case DfuStatus::errUSBR: {
				return "errUSBR";
			}

			case DfuStatus::errPOR: {
				return "errPOR";
			}

			case DfuStatus::errUNKNOWN: {
				return "errUNKNOWN";
			}

			case DfuStatus::errSTALLEDPKT: {
				return "errSTALLEDPKT";
			}
		}

		return "Unknown";
	}


	// Writes a 32-bit value as 0x and eight hex digits. A CRC printed this way reads the same as the one a PC tool prints.
	inline void WriteHex32(Print& out, uint32 value) {
		static constexpr char digits[] = "0123456789ABCDEF";
		out.Write("0x");
		for (int shift = 28; shift >= 0; shift -= 4) {
			out.Write(&digits[(value >> shift) & 0xF], 1);
		}
	}
}
