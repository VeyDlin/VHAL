#pragma once
#include <BSP.h>
#include <cstring>
#include <cmath>


class PGA460 {
private:
	AUART *serial;

	// Define UART commands by name
	// Single Address
	uint8 P1BL = 0x00;
	uint8 P2BL = 0x01;
	uint8 P1LO = 0x02;
	uint8 P2LO = 0x03;
	uint8 TNLM = 0x04;
	uint8 UMR = 0x05;
	uint8 TNLR = 0x06;
	uint8 TEDD = 0x07;
	uint8 SD = 0x08;
	uint8 SRR = 0x09;
	uint8 SRW = 0x0A;
	uint8 EEBR = 0x0B;
	uint8 EEBW = 0x0C;
	uint8 TVGBR = 0x0D;
	uint8 TVGBW = 0x0E;
	uint8 THRBR = 0x0F;
	uint8 THRBW = 0x10;
	// Broadcast
	uint8 BC_P1BL = 0x11;
	uint8 BC_P2BL = 0x12;
	uint8 BC_P1LO = 0x13;
	uint8 BC_P2LO = 0x14;
	uint8 BC_TNLM = 0x15;
	uint8 BC_RW = 0x16;
	uint8 BC_EEBW = 0x17;
	uint8 BC_TVGBW = 0x18;
	uint8 BC_THRBW = 0x19;
	// CMDs 26-31 are reserved

	// List user registers by name with default settings from TI factory
	uint8 USER_DATA1 = 0x00;
	uint8 USER_DATA2 = 0x00;
	uint8 USER_DATA3 = 0x00;
	uint8 USER_DATA4 = 0x00;
	uint8 USER_DATA5 = 0x00;
	uint8 USER_DATA6 = 0x00;
	uint8 USER_DATA7 = 0x00;
	uint8 USER_DATA8 = 0x00;
	uint8 USER_DATA9 = 0x00;
	uint8 USER_DATA10 = 0x00;
	uint8 USER_DATA11 = 0x00;
	uint8 USER_DATA12 = 0x00;
	uint8 USER_DATA13 = 0x00;
	uint8 USER_DATA14 = 0x00;
	uint8 USER_DATA15 = 0x00;
	uint8 USER_DATA16 = 0x00;
	uint8 USER_DATA17 = 0x00;
	uint8 USER_DATA18 = 0x00;
	uint8 USER_DATA19 = 0x00;
	uint8 USER_DATA20 = 0x00;
	uint8 TVGAIN0 = 0xAF;
	uint8 TVGAIN1 = 0xFF;
	uint8 TVGAIN2 = 0xFF;
	uint8 TVGAIN3 = 0x2D;
	uint8 TVGAIN4 = 0x68;
	uint8 TVGAIN5 = 0x36;
	uint8 TVGAIN6 = 0xFC;
	uint8 INIT_GAIN = 0xC0;
	uint8 FREQUENCY = 0x8C;
	uint8 DEADTIME = 0x00;
	uint8 PULSE_P1 = 0x01;
	uint8 PULSE_P2 = 0x12;
	uint8 CURR_LIM_P1 = 0x47;
	uint8 CURR_LIM_P2 = 0xFF;
	uint8 REC_LENGTH = 0x1C;
	uint8 FREQ_DIAG = 0x00;
	uint8 SAT_FDIAG_TH = 0xEE;
	uint8 FVOLT_DEC = 0x7C;
	uint8 DECPL_TEMP = 0x0A;
	uint8 DSP_SCALE = 0x00;
	uint8 TEMP_TRIM = 0x00;
	uint8 P1_GAIN_CTRL = 0x00;
	uint8 P2_GAIN_CTRL = 0x00;
	uint8 EE_CRC = 0xFF;
	uint8 EE_CNTRL = 0x00;
	uint8 P1_THR_0 = 0x88;
	uint8 P1_THR_1 = 0x88;
	uint8 P1_THR_2 = 0x88;
	uint8 P1_THR_3 = 0x88;
	uint8 P1_THR_4 = 0x88;
	uint8 P1_THR_5 = 0x88;
	uint8 P1_THR_6 = 0x84;
	uint8 P1_THR_7 = 0x21;
	uint8 P1_THR_8 = 0x08;
	uint8 P1_THR_9 = 0x42;
	uint8 P1_THR_10 = 0x10;
	uint8 P1_THR_11 = 0x80;
	uint8 P1_THR_12 = 0x80;
	uint8 P1_THR_13 = 0x80;
	uint8 P1_THR_14 = 0x80;
	uint8 P1_THR_15 = 0x80;
	uint8 P2_THR_0 = 0x88;
	uint8 P2_THR_1 = 0x88;
	uint8 P2_THR_2 = 0x88;
	uint8 P2_THR_3 = 0x88;
	uint8 P2_THR_4 = 0x88;
	uint8 P2_THR_5 = 0x88;
	uint8 P2_THR_6 = 0x84;
	uint8 P2_THR_7 = 0x21;
	uint8 P2_THR_8 = 0x08;
	uint8 P2_THR_9 = 0x42;
	uint8 P2_THR_10 = 0x10;
	uint8 P2_THR_11 = 0x80;
	uint8 P2_THR_12 = 0x80;
	uint8 P2_THR_13 = 0x80;
	uint8 P2_THR_14 = 0x80;
	uint8 P2_THR_15 = 0x80;

	// Miscellaneous variables; (+) indicates OWU transmitted uint8 offset
	uint8 checksum = 0x00; 				// UART checksum value
	uint8 ChecksumInput[44]; 			// data uint8 array for checksum calculator
	uint8 ultraMeasResult[34 + 3]; 		// data uint8 array for cmd5 and tciB+L return
	uint8 diagMeasResult[5 + 3]; 		// data uint8 array for cmd8 and index1 return
	uint8 tempNoiseMeasResult[4 + 3]; 	// data uint8 array for cmd6 and index0&1 return
	uint8 echoDataDump[130 + 3]; 		// data uint8 array for cmd7 and index12 return
	uint8 tempOrNoise = 0; 				// data uint8 to determine if temp or noise measurement is to be performed
	uint8 comm = 0; 						// indicates UART (0), TCI (1), OWU (2) communication mode
	uint8 bulkThr[34 + 3];				// data uint8 array for bulk threhsold commands

	// UART & OWU exclusive variables
	uint8 syncByte = 0x55; 		// data uint8 for Sync field set UART baud rate of PGA460
	uint8 regAddr = 0x00; 		// data uint8 for Register Address
	uint8 regData = 0x00; 		// data uint8 for Register Data
	uint8 uartAddr = 0; 			// PGA460 UART device address (0-7). '0' is factory default address
	uint8 numObj = 1; 			// number of objects to detect

	// OWU exclusive variables
	signed int owuShift = 0;	// accoutns for OWU receiver buffer offset for capturing master transmitted data - always 0 for standard two-wire UART

public:
	bool enableAutotsetThresholding = true;


public:
	PGA460() {}


	/*
	 	uart (IN) -- UART_HandleTypeDef
		mode (IN) -- sets communicaiton mode.
			0=UART
			1=TCI
			2=OWU
			3-...
		uartAddrUpdate (IN) -- PGA460 address range from 0 to 7
	*/
	PGA460(AUART &uart, uint8 mode, uint8 uartAddrUpdate) {
		serial = &uart;
		comm = mode;

		serial->SetParameters({ 115200, AUART::StopBits::B1, AUART::Parity::None });


		// check for valid UART address
		if(uartAddrUpdate > 7) {
			uartAddrUpdate = 0; // default to '0'
		}

		// globally update target PGA460 UART address and commands
		if(uartAddr != uartAddrUpdate) {

			// Update commands to account for new UART addr
			// Single Address
			P1BL = 0x00 + (uartAddrUpdate << 5);
			P2BL = 0x01 + (uartAddrUpdate << 5);
			P1LO = 0x02 + (uartAddrUpdate << 5);
			P2LO = 0x03 + (uartAddrUpdate << 5);
			TNLM = 0x04 + (uartAddrUpdate << 5);
			UMR = 0x05 + (uartAddrUpdate << 5);
			TNLR = 0x06 + (uartAddrUpdate << 5);
			TEDD = 0x07 + (uartAddrUpdate << 5);
			SD = 0x08 + (uartAddrUpdate << 5);
			SRR = 0x09 + (uartAddrUpdate << 5);
			SRW = 0x0A + (uartAddrUpdate << 5);
			EEBR = 0x0B + (uartAddrUpdate << 5);
			EEBW = 0x0C + (uartAddrUpdate << 5);
			TVGBR = 0x0D + (uartAddrUpdate << 5);
			TVGBW = 0x0E + (uartAddrUpdate << 5);
			THRBR = 0x0F + (uartAddrUpdate << 5);
			THRBW = 0x10 + (uartAddrUpdate << 5);
		}
		uartAddr = uartAddrUpdate;


		// OWU setup
		if(comm == 2) {
			PULSE_P1 = 0x80 | PULSE_P1; // update IO_IF_SEL bit to '1' for OWU mode for bulk EEPROM write

			// UART write to register PULSE_P1 (addr 0x1E) to set device into OWU mode
			regAddr = 0x1E;
			regData = PULSE_P1;
			uint8 buf10[5] = { syncByte, SRW, regAddr, regData, CalcChecksum(SRW) };
			SerialWrite((uint8*)buf10, sizeof(buf10));
			WaitWrite();
			System::DelayMs(50);
		}
	}





	/*
		0 = Murata MA58MF14-7N
		1 = Murata MA40H1S-R
	*/
	void SetDefaultSettings(uint8 xdcr) {
		switch (xdcr) {

			// Murata MA58MF14-7N
			case 0:
				USER_DATA1 = 0x00;
				USER_DATA2 = 0x00;
				USER_DATA3 = 0x00;
				USER_DATA4 = 0x00;
				USER_DATA5 = 0x00;
				USER_DATA6 = 0x00;
				USER_DATA7 = 0x00;
				USER_DATA8 = 0x00;
				USER_DATA9 = 0x00;
				USER_DATA10 = 0x00;
				USER_DATA11 = 0x00;
				USER_DATA12 = 0x00;
				USER_DATA13 = 0x00;
				USER_DATA14 = 0x00;
				USER_DATA15 = 0x00;
				USER_DATA16 = 0x00;
				USER_DATA17 = 0x00;
				USER_DATA18 = 0x00;
				USER_DATA19 = 0x00;
				USER_DATA20 = 0x00;
				TVGAIN0 = 0xAA;
				TVGAIN1 = 0xAA;
				TVGAIN2 = 0xAA;
				TVGAIN3 = 0x82;
				TVGAIN4 = 0x08;
				TVGAIN5 = 0x20;
				TVGAIN6 = 0x80;
				INIT_GAIN = 0x60;
				FREQUENCY = 0x8F;
				DEADTIME = 0xA0;
				if (comm == 2) {
					PULSE_P1 = 0x80 | 0x04;
				} else {
					PULSE_P1 = 0x04;
				}
				PULSE_P2 = 0x10;
				CURR_LIM_P1 = 0x55;
				CURR_LIM_P2 = 0x55;
				REC_LENGTH = 0x19;
				FREQ_DIAG = 0x33;
				SAT_FDIAG_TH = 0xEE;
				FVOLT_DEC = 0x7C;
				DECPL_TEMP = 0x4F;
				DSP_SCALE = 0x00;
				TEMP_TRIM = 0x00;
				P1_GAIN_CTRL = 0x09;
				P2_GAIN_CTRL = 0x09;
			break;

			// Murata MA40H1SR
			case 1:
				USER_DATA1 = 0x00;
				USER_DATA2 = 0x00;
				USER_DATA3 = 0x00;
				USER_DATA4 = 0x00;
				USER_DATA5 = 0x00;
				USER_DATA6 = 0x00;
				USER_DATA7 = 0x00;
				USER_DATA8 = 0x00;
				USER_DATA9 = 0x00;
				USER_DATA10 = 0x00;
				USER_DATA11 = 0x00;
				USER_DATA12 = 0x00;
				USER_DATA13 = 0x00;
				USER_DATA14 = 0x00;
				USER_DATA15 = 0x00;
				USER_DATA16 = 0x00;
				USER_DATA17 = 0x00;
				USER_DATA18 = 0x00;
				USER_DATA19 = 0x00;
				USER_DATA20 = 0x00;
				TVGAIN0 = 0xAA;
				TVGAIN1 = 0xAA;
				TVGAIN2 = 0xAA;
				TVGAIN3 = 0x51;
				TVGAIN4 = 0x45;
				TVGAIN5 = 0x14;
				TVGAIN6 = 0x50;
				INIT_GAIN = 0x54;
				FREQUENCY = 0x32;
				DEADTIME = 0xA0;
				if (comm == 2) {
					PULSE_P1 = 0x80 | 0x08;
				} else {
					PULSE_P1 = 0x08;
				}
				PULSE_P2 = 0x10;
				CURR_LIM_P1 = 0x40;
				CURR_LIM_P2 = 0x40;
				REC_LENGTH = 0x19;
				FREQ_DIAG = 0x33;
				SAT_FDIAG_TH = 0xEE;
				FVOLT_DEC = 0x7C;
				DECPL_TEMP = 0x4F;
				DSP_SCALE = 0x00;
				TEMP_TRIM = 0x00;
				P1_GAIN_CTRL = 0x09;
				P2_GAIN_CTRL = 0x09;
			break;

			// UTR-1440K-TT-R
			case 2: {
					USER_DATA1 = 0x00;
					USER_DATA2 = 0x00;
					USER_DATA3 = 0x00;
					USER_DATA4 = 0x00;
					USER_DATA5 = 0x00;
					USER_DATA6 = 0x00;
					USER_DATA7 = 0x00;
					USER_DATA8 = 0x00;
					USER_DATA9 = 0x00;
					USER_DATA10 = 0x00;
					USER_DATA11 = 0x00;
					USER_DATA12 = 0x00;
					USER_DATA13 = 0x00;
					USER_DATA14 = 0x00;
					USER_DATA15 = 0x00;
					USER_DATA16 = 0x00;
					USER_DATA17 = 0x00;
					USER_DATA18 = 0x00;
					USER_DATA19 = 0x00;
					USER_DATA20 = 0x00;

					TVGAIN0 = 0xAF;
					TVGAIN1 = 0xFF;
					TVGAIN2 = 0xFF;
					TVGAIN3 = 0x45;
					TVGAIN4 = 0x68;
					TVGAIN5 = 0x36;
					TVGAIN6 = 0xFC;

					INIT_GAIN = 0x5F;
					FREQUENCY = 0x32;
					DEADTIME = 0x00;
					if (comm == 2) {
						PULSE_P1 = 0x80 | 0x0F;
					} else {
						PULSE_P1 = 0x0F;
					}
					PULSE_P2 = 0x12;
					CURR_LIM_P1 = 0x5F;
					CURR_LIM_P2 = 0x7F;
					REC_LENGTH = 0x8C;
					FREQ_DIAG = 0x00;
					SAT_FDIAG_TH = 0xEE;
					FVOLT_DEC = 0x7C;
					DECPL_TEMP = 0x0A;
					DSP_SCALE = 0x00;
					TEMP_TRIM = 0x00;
					P1_GAIN_CTRL = 0x08;
					P2_GAIN_CTRL = 0x00;
					break;
				}
			default: break;
		}

		uint8 buf12[46] = {
			syncByte, EEBW, USER_DATA1, USER_DATA2, USER_DATA3, USER_DATA4, USER_DATA5, USER_DATA6,
			USER_DATA7, USER_DATA8, USER_DATA9, USER_DATA10, USER_DATA11, USER_DATA12, USER_DATA13, USER_DATA14,
			USER_DATA15,USER_DATA16,USER_DATA17,USER_DATA18,USER_DATA19,USER_DATA20,
			TVGAIN0,TVGAIN1,TVGAIN2,TVGAIN3,TVGAIN4,TVGAIN5,TVGAIN6,INIT_GAIN,FREQUENCY,DEADTIME,
			PULSE_P1,PULSE_P2,CURR_LIM_P1,CURR_LIM_P2,REC_LENGTH,FREQ_DIAG,SAT_FDIAG_TH,FVOLT_DEC,DECPL_TEMP,
			DSP_SCALE,TEMP_TRIM,P1_GAIN_CTRL,P2_GAIN_CTRL,CalcChecksum(EEBW)
		};


		SerialWrite((uint8*)buf12, sizeof(buf12)); // serial transmit master data for bulk EEPROM
		WaitWrite();
		System::DelayMs(50);

		// Update targeted UART_ADDR to address defined in EEPROM bulk switch-case
		uint8 uartAddrUpdate = (PULSE_P2 >> 5) & 0x07;
		if(uartAddr != uartAddrUpdate) {

			// Update commands to account for new UART addr
			// Single Address
			P1BL = 0x00 + (uartAddrUpdate << 5);
			P2BL = 0x01 + (uartAddrUpdate << 5);
			P1LO = 0x02 + (uartAddrUpdate << 5);
			P2LO = 0x03 + (uartAddrUpdate << 5);
			TNLM = 0x04 + (uartAddrUpdate << 5);
			UMR = 0x05 + (uartAddrUpdate << 5);
			TNLR = 0x06 + (uartAddrUpdate << 5);
			TEDD = 0x07 + (uartAddrUpdate << 5);
			SD = 0x08 + (uartAddrUpdate << 5);
			SRR = 0x09 + (uartAddrUpdate << 5);
			SRW = 0x0A + (uartAddrUpdate << 5);
			EEBR = 0x0B + (uartAddrUpdate << 5);
			EEBW = 0x0C + (uartAddrUpdate << 5);
			TVGBR = 0x0D + (uartAddrUpdate << 5);
			TVGBW = 0x0E + (uartAddrUpdate << 5);
			THRBR = 0x0F + (uartAddrUpdate << 5);
			THRBW = 0x10 + (uartAddrUpdate << 5);
		}
		uartAddr = uartAddrUpdate;
	}





	/*
		thr (IN) -- updates all threshold levels to a fixed level based on specific percentage of the maximum level.
			All times are mid-code (1.4ms intervals).
			Modify existing case statements, or append additional case-statement for custom user threshold configurations.
			• 0 = 25% Levels 64 of 255
			• 1 = 50% Levels 128 of 255
			• 2 = 75% Levels 192 of 255
	*/
	void InitThresholds(uint8 thr) {
		switch (thr) {

			// 25% Levels 64 of 255
			case 0:
				P1_THR_0 = 0x88;
				P1_THR_1 = 0x88;
				P1_THR_2 = 0x88;
				P1_THR_3 = 0x88;
				P1_THR_4 = 0x88;
				P1_THR_5 = 0x88;
				P1_THR_6 = 0x42;
				P1_THR_7 = 0x10;
				P1_THR_8 = 0x84;
				P1_THR_9 = 0x21;
				P1_THR_10 = 0x08;
				P1_THR_11 = 0x40;
				P1_THR_12 = 0x40;
				P1_THR_13 = 0x40;
				P1_THR_14 = 0x40;
				P1_THR_15 = 0x00;
				P2_THR_0 = 0x88;
				P2_THR_1 = 0x88;
				P2_THR_2 = 0x88;
				P2_THR_3 = 0x88;
				P2_THR_4 = 0x88;
				P2_THR_5 = 0x88;
				P2_THR_6 = 0x42;
				P2_THR_7 = 0x10;
				P2_THR_8 = 0x84;
				P2_THR_9 = 0x21;
				P2_THR_10 = 0x08;
				P2_THR_11 = 0x40;
				P2_THR_12 = 0x40;
				P2_THR_13 = 0x40;
				P2_THR_14 = 0x40;
				P2_THR_15 = 0x00;
			break;

			// 50% Level (midcode) 128 of 255
			case 1:
				P1_THR_0 = 0x88;
				P1_THR_1 = 0x88;
				P1_THR_2 = 0x88;
				P1_THR_3 = 0x88;
				P1_THR_4 = 0x88;
				P1_THR_5 = 0x88;
				P1_THR_6 = 0x84;
				P1_THR_7 = 0x21;
				P1_THR_8 = 0x08;
				P1_THR_9 = 0x42;
				P1_THR_10 = 0x10;
				P1_THR_11 = 0x80;
				P1_THR_12 = 0x80;
				P1_THR_13 = 0x80;
				P1_THR_14 = 0x80;
				P1_THR_15 = 0x00;
				P2_THR_0 = 0x88;
				P2_THR_1 = 0x88;
				P2_THR_2 = 0x88;
				P2_THR_3 = 0x88;
				P2_THR_4 = 0x88;
				P2_THR_5 = 0x88;
				P2_THR_6 = 0x84;
				P2_THR_7 = 0x21;
				P2_THR_8 = 0x08;
				P2_THR_9 = 0x42;
				P2_THR_10 = 0x10;
				P2_THR_11 = 0x80;
				P2_THR_12 = 0x80;
				P2_THR_13 = 0x80;
				P2_THR_14 = 0x80;
				P2_THR_15 = 0x00;
			break;

			// 75% Levels 192 of 255
			case 2:
				P1_THR_0 = 0x88;
				P1_THR_1 = 0x88;
				P1_THR_2 = 0x88;
				P1_THR_3 = 0x88;
				P1_THR_4 = 0x88;
				P1_THR_5 = 0x88;
				P1_THR_6 = 0xC6;
				P1_THR_7 = 0x31;
				P1_THR_8 = 0x8C;
				P1_THR_9 = 0x63;
				P1_THR_10 = 0x18;
				P1_THR_11 = 0xC0;
				P1_THR_12 = 0xC0;
				P1_THR_13 = 0xC0;
				P1_THR_14 = 0xC0;
				P1_THR_15 = 0x00;
				P2_THR_0 = 0x88;
				P2_THR_1 = 0x88;
				P2_THR_2 = 0x88;
				P2_THR_3 = 0x88;
				P2_THR_4 = 0x88;
				P2_THR_5 = 0x88;
				P2_THR_6 = 0xC6;
				P2_THR_7 = 0x31;
				P2_THR_8 = 0x8C;
				P2_THR_9 = 0x63;
				P2_THR_10 = 0x18;
				P2_THR_11 = 0xC0;
				P2_THR_12 = 0xC0;
				P2_THR_13 = 0xC0;
				P2_THR_14 = 0xC0;
				P2_THR_15 = 0x00;
			break;

			// UTR-1440K-TT-R
			case 3:
				P1_THR_0 = 0x88;
				P1_THR_1 = 0x88;
				P1_THR_2 = 0x88;
				P1_THR_3 = 0x88;
				P1_THR_4 = 0x88;
				P1_THR_5 = 0x88;
				P1_THR_6 = 0x84;
				P1_THR_7 = 0x21;
				P1_THR_8 = 0x08;
				P1_THR_9 = 0x42;
				P1_THR_10 = 0x10;
				P1_THR_11 = 0x80;
				P1_THR_12 = 0x80;
				P1_THR_13 = 0x80;
				P1_THR_14 = 0x80;
				P1_THR_15 = 0x00;
				P2_THR_0 = 0x88;
				P2_THR_1 = 0x88;
				P2_THR_2 = 0x88;
				P2_THR_3 = 0x88;
				P2_THR_4 = 0x88;
				P2_THR_5 = 0x88;
				P2_THR_6 = 0x84;
				P2_THR_7 = 0x21;
				P2_THR_8 = 0x08;
				P2_THR_9 = 0x42;
				P2_THR_10 = 0x10;
				P2_THR_11 = 0x80;
				P2_THR_12 = 0x80;
				P2_THR_13 = 0x80;
				P2_THR_14 = 0x80;
				P2_THR_15 = 0x00;
			break;

			default: break;
		}

		uint8 buf16[35] = {
			syncByte, THRBW, P1_THR_0, P1_THR_1, P1_THR_2, P1_THR_3, P1_THR_4, P1_THR_5, P1_THR_6,
			P1_THR_7, P1_THR_8, P1_THR_9, P1_THR_10, P1_THR_11, P1_THR_12, P1_THR_13, P1_THR_14, P1_THR_15,
			P2_THR_0, P2_THR_1, P2_THR_2, P2_THR_3, P2_THR_4, P2_THR_5, P2_THR_6,
			P2_THR_7, P2_THR_8, P2_THR_9, P2_THR_10, P2_THR_11, P2_THR_12, P2_THR_13, P2_THR_14, P2_THR_15,
			CalcChecksum(THRBW)
		};


		SerialWrite((uint8*)buf16, sizeof(buf16)); // serial transmit master data for bulk threhsold
		WaitWrite();
		System::DelayMs(100);
	}





	/*
		agr (IN) -- updates the analog gain range for the TVG.
			• 0 = 32-64dB
			• 1 = 46-78dB
			• 2 = 52-84dB
			• 3 = 58-90dB
		tvg (IN) -- updates all TVG levels to a fixed level based on specific percentage of the maximum level.
			All times are mid-code (2.4ms intervals).
			Modify existing case statements, or append additional case-statement for custom user TVG configurations
			• 0 = none
			• 1 = 25% Levels of range
			• 2 = 50% Levels of range
			• 3 = 75% Levels of range
	*/
	void InitTvg(uint8 agr, uint8 tvg) {
		uint8 gain_range = 0x4F;

		// set AFE gain range
		switch(agr) {
			case 3: gain_range = 0x0F; break; // 58-90dB
			case 2: gain_range = 0x4F; break; // 52-84dB
			case 1: gain_range = 0x8F; break; // 46-78dB
			case 0: gain_range = 0xCF; break; // 32-64dB
			default: break;
		}


		regAddr = 0x26;
		regData = gain_range;
		uint8 buf10[5] = { syncByte, SRW, regAddr, regData, CalcChecksum(SRW) };
		SerialWrite((uint8*)buf10, sizeof(buf10));
		WaitWrite();

		// Set fixed AFE gain value
		switch(tvg) {

			// none
			case 0: break;

			// 25% Level
			case 1:
				TVGAIN0 = 0x88;
				TVGAIN1 = 0x88;
				TVGAIN2 = 0x88;
				TVGAIN3 = 0x41;
				TVGAIN4 = 0x04;
				TVGAIN5 = 0x10;
				TVGAIN6 = 0x40;
			break;

			// 50% Levels
			case 2:
				TVGAIN0 = 0x88;
				TVGAIN1 = 0x88;
				TVGAIN2 = 0x88;
				TVGAIN3 = 0x82;
				TVGAIN4 = 0x08;
				TVGAIN5 = 0x20;
				TVGAIN6 = 0x80;
			break;

			// 75% Levels
			case 3:
				TVGAIN0 = 0x88;
				TVGAIN1 = 0x88;
				TVGAIN2 = 0x88;
				TVGAIN3 = 0xC3;
				TVGAIN4 = 0x0C;
				TVGAIN5 = 0x30;
				TVGAIN6 = 0xC0;
			break;

			default: break;
		}


		uint8 buf14[10] = { syncByte, TVGBW, TVGAIN0, TVGAIN1, TVGAIN2, TVGAIN3, TVGAIN4, TVGAIN5, TVGAIN6, CalcChecksum(TVGBW) };
		SerialWrite((uint8*)buf14, sizeof(buf14)); // serial transmit master data for bulk TVG
		WaitWrite();
	}





	/*
		cmd (IN) -- determines which preset command is run
			• 0 = Preset 1 Burst + Listen command
			• 1 = Preset 2 Burst + Listen command
			• 2 = Preset 1 Listen Only command
			• 3 = Preset 2 Listen Only command
			• 17 = Preset 1 Burst + Listen broadcast command
			• 18 = Preset 2 Burst + Listen broadcast command
			• 19 = Preset 1 Listen Only broadcast command
			• 20 = Preset 2 Listen Only broadcast command
		numObjUpdate (IN) -- PGA460 can capture time-of-flight, width, and amplitude for 1 to 8 objects.
			TCI is limited to time-of-flight measurement data only.
	*/
	void UltrasonicCmd(uint8 cmd, uint8 numObjUpdate) {
		numObj = numObjUpdate; // number of objects to detect
		uint8 bufCmd[4] = { syncByte, 0xFF, numObj, 0xFF }; // prepare bufCmd with 0xFF placeholders

		switch(cmd) {

			// Send Preset 1 Burst + Listen command
			case 0: {
				bufCmd[1] = P1BL;
				bufCmd[3] = CalcChecksum(P1BL);
			}
			break;

			// Send Preset 2 Burst + Listen command
			case 1: {
				bufCmd[1] = P2BL;
				bufCmd[3] = CalcChecksum(P2BL);
			}
			break;

			// Send Preset 1 Listen Only command
			case 2: {
				bufCmd[1] = P1LO;
				bufCmd[3] = CalcChecksum(P1LO);
			}
			break;

			// Send Preset 2 Listen Only command
			case 3: {
				bufCmd[1] = P2LO;
				bufCmd[3] = CalcChecksum(P2LO);
			}
			break;

			// Send Preset 1 Burst + Listen Broadcast command
			case 17: {
				bufCmd[1] = BC_P1BL;
				bufCmd[3] = CalcChecksum(BC_P1BL);
			}
			break;

			// Send Preset 2 Burst + Listen Broadcast command
			case 18: {
				bufCmd[1] = BC_P2BL;
				bufCmd[3] = CalcChecksum(BC_P2BL);
			}
			break;

			// Send Preset 1 Listen Only Broadcast command
			case 19: {
				bufCmd[1] = BC_P1LO;
				bufCmd[3] = CalcChecksum(BC_P1LO);
			}
			break;

			// Send Preset 2 Listen Only Broadcast command
			case 20: {
				bufCmd[1] = BC_P2LO;
				bufCmd[3] = CalcChecksum(BC_P2LO);
			}
			break;

			default: return;
		}


		SerialWrite((uint8*)bufCmd, sizeof(bufCmd)); // serial transmit master data to initiate burst and/or listen command
		WaitWrite();

		uint8 recordLength;
		if(cmd == 1 || cmd == 2 || cmd == 17 || cmd == 19) {
			recordLength = (1 + ((0x8C & 0xF0) >> 4)) * 4.096;
		} else {
			recordLength = (1 + (0x8C & 0x0F)) * 4.096;
		}

		System::DelayMs(recordLength + 5); // maximum record length is 65ms, so DelayMs with margin
	}





	/*
		busDemo (IN) -- When true, do not print error message for a failed reading when running bus demo
	*/
	bool PullUltrasonicMeasResult() {
		memset(ultraMeasResult, 0, sizeof(ultraMeasResult));

		WaitRead();
		if(SerialRead((uint8*)ultraMeasResult, (2 + (numObj * 4)) + owuShift) == false) {
			return false;
		}


		if(comm == 2) {
			owuShift = 2; // OWU receive buffer offset to ignore transmitted data
		}

		uint8 buf5[3] = { syncByte, UMR, CalcChecksum(UMR) };
		SerialWrite((uint8*)buf5, sizeof(buf5)); //serial transmit master data to read ultrasonic measurement results
		WaitWrite();

		WaitRead();

		// OWU mode only
		if(comm == 2) {
			// rearrange array for OWU UMR results
			for(int n = 0; n < 2 + (numObj * 4); n++) {
				ultraMeasResult[n + 1] = ultraMeasResult[n + owuShift]; // element 0 skipped due to no diagnostic field returned
			}
		}

		return true;
	}





	/*
		umr (IN) -- Ultrasonic measurement result look-up selector:
				Distance (m)	Width	Amplitude
				--------------------------------
			Obj1		0		1		2
			Obj2		3		4		5
			Obj3		6		7		8
			Obj4		9		10		11
			Obj5		12		13		14
			Obj6		15		16		17
			Obj7		18		19		20
			Obj8		21		22		23
	*/
	double PrintUltrasonicMeasResult(uint8 umr) {
		int speedSound = 343; // speed of sound in air at room temperature

		double objReturn = 0;
		double digitalDelay = 0; // TODO: compensates the burst time calculated as number_of_pulses/frequency.
		uint16_t objDist = 0;
		uint16_t objWidth = 0;
		uint16_t objAmp = 0;

		switch (umr) {

			// Obj1 Distance (m)
			case 0: {
				objDist = (ultraMeasResult[1] << 8) + ultraMeasResult[2];
				objReturn = (objDist / 2 * 0.000001 * speedSound) - digitalDelay;
			}
			break;

			// Obj1 Width (us)
			case 1: {
				objWidth = ultraMeasResult[3];
				objReturn = objWidth * 16;
			}
			break;

			// Obj1 Peak Amplitude
			case 2:{
				objAmp = ultraMeasResult[4];
				objReturn = objAmp;
			}
			break;

			// Obj2 Distance (m)
			case 3: {
				objDist = (ultraMeasResult[5] << 8) + ultraMeasResult[6];
				objReturn = (objDist / 2 * 0.000001 * speedSound) - digitalDelay;
			}
			break;

			// Obj2 Width (us)
			case 4:{
				objWidth = ultraMeasResult[7];
				objReturn = objWidth * 16;
			}
			break;

			// Obj2 Peak Amplitude
			case 5: {
				objAmp = ultraMeasResult[8];
				objReturn = objAmp;
			}
			break;

			// Obj3 Distance (m)
			case 6: {
				objDist = (ultraMeasResult[9] << 8) + ultraMeasResult[10];
				objReturn = (objDist / 2 * 0.000001 * speedSound) - digitalDelay;
			}
			break;

			// Obj3 Width (us)
			case 7: {
				objWidth = ultraMeasResult[11];
				objReturn = objWidth * 16;
			}
			break;

			// Obj3 Peak Amplitude
			case 8: {
				objAmp = ultraMeasResult[12];
				objReturn = objAmp;
			}
			break;

			// Obj4 Distance (m)
			case 9: {
				objDist = (ultraMeasResult[13] << 8) + ultraMeasResult[14];
				objReturn = (objDist / 2 * 0.000001 * speedSound) - digitalDelay;
			}
			break;

			// Obj4 Width (us)
			case 10: {
				objWidth = ultraMeasResult[15];
				objReturn = objWidth * 16;
			}
			break;

			// Obj4 Peak Amplitude
			case 11: {
				objAmp = ultraMeasResult[16];
				objReturn = objAmp;
			}
			break;

			// Obj5 Distance (m)
			case 12: {
				objDist = (ultraMeasResult[17] << 8) + ultraMeasResult[18];
				objReturn = (objDist / 2 * 0.000001 * speedSound) - digitalDelay;
			}
			break;

			// Obj5 Width (us)
			case 13: {
				objWidth = ultraMeasResult[19];
				objReturn = objWidth * 16;
			}
			break;

			// Obj5 Peak Amplitude
			case 14: {
				objAmp = ultraMeasResult[20];
				objReturn = objAmp;
			}
			break;

			// Obj6 Distance (m)
			case 15: {
				objDist = (ultraMeasResult[21] << 8) + ultraMeasResult[22];
				objReturn = (objDist / 2 * 0.000001 * speedSound) - digitalDelay;
			}
			break;

			// Obj6 Width (us)
			case 16: {
				objWidth = ultraMeasResult[23];
				objReturn = objWidth * 16;
			}
			break;

			// Obj6 Peak Amplitude
			case 17: {
				objAmp = ultraMeasResult[24];
				objReturn = objAmp;
			}
			break;

			// Obj7 Distance (m)
			case 18: {
				objDist = (ultraMeasResult[25] << 8) + ultraMeasResult[26];
				objReturn = (objDist / 2 * 0.000001 * speedSound) - digitalDelay;
			}
			break;

			// Obj7 Width (us)
			case 19: {
				objWidth = ultraMeasResult[27];
				objReturn = objWidth * 16;
			}
			break;

			// Obj7 Peak Amplitude
			case 20: {
				objAmp = ultraMeasResult[28];
				objReturn = objAmp;
			}
			break;

			// Obj8 Distance (m)
			case 21: {
				objDist = (ultraMeasResult[29] << 8) + ultraMeasResult[30];
				objReturn = (objDist / 2 * 0.000001 * speedSound) - digitalDelay;
			}
			break;

			// Obj8 Width (us)
			case 22: {
				objWidth = ultraMeasResult[31];
				objReturn = objWidth * 16;
			}
			break;

			// Obj8 Peak Amplitude
			case 23: {
				objAmp = ultraMeasResult[32];
				objReturn = objAmp;
			}
			break;

			default: break;
		}
		return objReturn;
	}





	uint8 PrintUltrasonicMeasResultRaw(uint8 umr) {
		return ultraMeasResult[umr];
	}







	/*
		preset (IN) -- determines which preset command is run:
			• 0 = Preset 1 Burst + Listen command
			• 1 = Preset 2 Burst + Listen command
			• 2 = Preset 1 Listen Only command
			• 3 = Preset 2 Listen Only command
			• 17 = Preset 1 Burst + Listen broadcast command
			• 18 = Preset 2 Burst + Listen broadcast command
			• 19 = Preset 1 Listen Only broadcast command
			• 20 = Preset 2 Listen Only broadcast command
	*/
	void RunEchoDataDump(uint8 preset) {

		// enable Echo Data Dump bit
		regAddr = 0x40;
		regData = 0x80;
		uint8 writeType = SRW; // default to single address register write (cmd10)

		// update to broadcast register write if broadcast TOF preset command given
		if(preset > 16) {
			writeType = BC_RW; // cmd22
		}

		uint8 buf10[5] = { syncByte, writeType, regAddr, regData, CalcChecksum(writeType) };
		SerialWrite((uint8*)buf10, sizeof(buf10));
		WaitWrite();
		System::DelayMs(10);

		// run preset 1 or 2 burst and or listen command
		UltrasonicCmd(preset, 1);

		// disbale Echo Data Dump bit
		regData = 0x00;
		buf10[3] = regData;
		buf10[4] = CalcChecksum(writeType);
		SerialWrite((uint8*)buf10, sizeof(buf10));
		WaitWrite();
	}





	/*
		element (IN) -- element from the 128 uint8 EDD memory
	*/
	uint8 PullEchoDataDump(uint8 element) {
		if(element == 0) {
			if(comm == 2) {
				owuShift = 2; // OWU receive buffer offset to ignore transmitted data
			} else {
				owuShift = 0;
			}

			regAddr = 0x80; // start of EDD memory
			uint8 buf9[4] = { syncByte, SRR, regAddr, CalcChecksum(SRR) };
			SerialWrite((uint8*)buf9, sizeof(buf9)); // read first uint8 of EDD memory
			WaitWrite();

			// loop readout by iterating through EDD address range
			for(uint8 m = 0; m < 129; m++) {
				buf9[2] = regAddr;
				buf9[3] = CalcChecksum(SRR);
				SerialWrite((uint8*)buf9, sizeof(buf9));
				WaitWrite();
				System::DelayMs(30);

				uint8 buf130[130];
				SerialRead((uint8*)buf130, 128 + owuShift);
				WaitRead();
				echoDataDump[m] = buf130[1 + owuShift];

				regAddr++;
			}
		}
		return echoDataDump[element];
	}





	/*
		run (IN) -- issue a preset 1 burst-and-listen command
		diag (IN) -- diagnostic value to return:
			• 0 = frequency diagnostic (kHz)
			• 1 = decay period diagnostic (us)
			• 2 = die temperature (degC)
			• 3 = noise level (8bit)
	*/
	double RunDiagnostics(uint8 run, uint8 diag) {
		double diagReturn = 0;
		int elementOffset = 0; // Only non-zero for OWU mode.
		int owuShiftSysDiag = 0; // Only non-zero for OWU mode.

		if(comm == 2) {
			owuShift = 2; // OWU receive buffer offset to ignore transmitted data
			owuShiftSysDiag = 1;
		}

		// issue  P1 burst+listen, and run system diagnostics command to get latest results
		if(run == 1) {
			// run burst+listen command at least once for proper diagnostic analysis
			UltrasonicCmd(0, 1);	// always run preset 1 (short distance) burst+listen for 1 object for system diagnostic


			// record time length maximum of 65ms, so add margin
			System::DelayMs(100);

			uint8 buf8[3] = { syncByte, SD, CalcChecksum(SD) };
			SerialWrite((uint8*)buf8, sizeof(buf8)); // serial transmit master data to read system diagnostic results
			WaitWrite();

			SerialRead((uint8*)diagMeasResult, 4 + owuShift - owuShiftSysDiag);
			WaitRead();
		}

		// run temperature measurement
		if(diag == 2) {
			tempOrNoise = 0; // temp meas
			uint8 buf4[4] = { syncByte, TNLM, tempOrNoise, CalcChecksum(TNLM) };
			SerialWrite((uint8*)buf4, sizeof(buf4)); // serial transmit master data to run temp measurement
			WaitWrite();
			System::DelayMs(20);

			uint8 buf6[3] = { syncByte, TNLR, CalcChecksum(TNLR) };
			SerialWrite((uint8*)buf6, sizeof(buf6)); // serial transmit master data to read temperature and noise results
			WaitWrite();
			System::DelayMs(100);
		}


		// run noise level meas
		if(diag == 3) {
			tempOrNoise = 1; // noise meas
			uint8 buf4[4] = { syncByte, TNLM, tempOrNoise, CalcChecksum(TNLM) };
			SerialWrite((uint8*)buf4, sizeof(buf4)); // serial transmit master data to run noise level measurement (requires at least 8.2ms of post-DelayMs)
			WaitWrite();
			System::DelayMs(20);

			uint8 buf6[3] = { syncByte, TNLR, CalcChecksum(TNLR) }; // serial transmit master data to read temperature and noise results
			SerialWrite((uint8*)buf6, sizeof(buf6));
			WaitWrite();
			System::DelayMs(100);
		}


		// pull temp and noise level results
		if(diag == 2 || diag == 3) {
			SerialRead((uint8*)tempNoiseMeasResult, 4 + owuShift - owuShiftSysDiag);
			WaitRead();
		}
		elementOffset = owuShift - owuShiftSysDiag; // OWU only

		System::DelayMs(100);


		switch(diag) {

			// convert to transducer frequency in kHz
			case 0: {
				diagReturn = (1 / (diagMeasResult[1 + elementOffset] * 0.0000005)) / 1000;
			}
			break;

			// convert to decay period time in us
			case 1: {
				diagReturn = diagMeasResult[2 + elementOffset] * 16;
			}
			break;

			// convert to temperature in degC
			case 2: {
				diagReturn = (tempNoiseMeasResult[1 + elementOffset] - 64) / 1.5;
			}
			break;

			// noise floor level
			case 3:{
				diagReturn = tempNoiseMeasResult[2 + elementOffset];
			}
			break;

			default: break;
		}

		return diagReturn;
	}





	/*
		Burns the EEPROM to preserve the working/shadow register values to EEPROM after power
		cycling the PGA460 device. Returns EE_PGRM_OK bit to determine if EEPROM burn was successful.
	*/
	bool BurnEeprom() {
		uint8 burnStat = 0;
		bool burnSuccess = false;

		// Write "0xD" to EE_UNLCK to unlock EEPROM, and '0' to EEPRGM bit at EE_CNTRL register
		regAddr = 0x40; //EE_CNTRL
		regData = 0x68;
		uint8 buf10[5] = { syncByte, SRW, regAddr, regData, CalcChecksum(SRW) };
		SerialWrite((uint8*)buf10, sizeof(buf10));
		WaitWrite();
		System::DelayMs(1);

		// Write "0xD" to EE_UNLCK to unlock EEPROM, and '1' to EEPRGM bit at EE_CNTRL register
		regAddr = 0x40; // EE_CNTRL
		regData = 0x69;
		buf10[2] = regAddr;
		buf10[3] = regData;
		buf10[4] = CalcChecksum(SRW);
		SerialWrite((uint8*)buf10, sizeof(buf10));
		WaitWrite();
		System::DelayMs(1000);


		// Read back EEPROM program status
		if(comm == 2) {
			owuShift = 1; // OWU receive buffer offset to ignore transmitted data
		}

		regAddr = 0x40; // EE_CNTRL
		uint8 buf9[4] = { syncByte, SRR, regAddr, CalcChecksum(SRR) };
		SerialWrite((uint8*)buf9, sizeof(buf9));
		WaitWrite();
		System::DelayMs(10);


		uint8 buf3[3];
		SerialRead((uint8*)buf3, 3);
		WaitRead();
		burnStat = buf3[1 - owuShift]; // store EE_CNTRL data


		// check if EE_PGRM_OK bit is '1'
		if((burnStat & 0x04) == 0x04) {
			burnSuccess = true;
		}

		return burnSuccess;
	}





	/*
		eeBulk (IN) -- if true, broadcast user EEPROM
		tvgBulk (IN) -- if true, broadcast TVG
		thrBulk (IN) -- if true, broadcast Threshold
	*/
	void Broadcast(bool eeBulk, bool tvgBulk, bool thrBulk) {

		// TVG broadcast command:
		if(tvgBulk) {
			uint8 buf24[10] = { syncByte, BC_TVGBW, TVGAIN0, TVGAIN1, TVGAIN2, TVGAIN3, TVGAIN4, TVGAIN5, TVGAIN6, CalcChecksum(BC_TVGBW) };
			SerialWrite((uint8*)buf24, sizeof(buf24));
			WaitWrite();
			System::DelayMs(10);
		}

		// Threshold broadcast command:
		if(thrBulk) {
			uint8 buf25[35] = {
				syncByte, BC_THRBW, P1_THR_0, P1_THR_1, P1_THR_2, P1_THR_3, P1_THR_4, P1_THR_5, P1_THR_6,
				P1_THR_7, P1_THR_8, P1_THR_9, P1_THR_10, P1_THR_11, P1_THR_12, P1_THR_13, P1_THR_14, P1_THR_15,
				P2_THR_0, P2_THR_1, P2_THR_2, P2_THR_3, P2_THR_4, P2_THR_5, P2_THR_6,
				P2_THR_7, P2_THR_8, P2_THR_9, P2_THR_10, P2_THR_11, P2_THR_12, P2_THR_13, P2_THR_14, P2_THR_15,
				CalcChecksum(BC_THRBW)
			};
			SerialWrite((uint8*)buf25, sizeof(buf25));
			WaitWrite();
			System::DelayMs(10);
		}

		// User EEPROM broadcast command (placeholder):
		if (eeBulk) {
			uint8 buf23[46] = {
				syncByte, BC_EEBW, USER_DATA1, USER_DATA2, USER_DATA3, USER_DATA4, USER_DATA5, USER_DATA6,
				USER_DATA7, USER_DATA8, USER_DATA9, USER_DATA10, USER_DATA11, USER_DATA12, USER_DATA13, USER_DATA14,
				USER_DATA15, USER_DATA16, USER_DATA17, USER_DATA18, USER_DATA19, USER_DATA20,
				TVGAIN0, TVGAIN1, TVGAIN2, TVGAIN3, TVGAIN4, TVGAIN5, TVGAIN6,INIT_GAIN, FREQUENCY, DEADTIME,
				PULSE_P1, PULSE_P2, CURR_LIM_P1, CURR_LIM_P2, REC_LENGTH, FREQ_DIAG, SAT_FDIAG_TH, FVOLT_DEC, DECPL_TEMP,
				DSP_SCALE, TEMP_TRIM, P1_GAIN_CTRL, P2_GAIN_CTRL,
				CalcChecksum(BC_EEBW)
			};
			SerialWrite((uint8*)buf23, sizeof(buf23));
			WaitWrite();
			System::DelayMs(50);
		}
	}





	/*
		Calculates the UART checksum value based on the selected command and the user EERPOM values associated with the command
		This function is not applicable to TCI mode.
	*/
	uint8 CalcChecksum(uint8 cmd) {
		int checksumLoops = 0;

		cmd = cmd & 0x001F; // zero-mask command address of cmd to select correct switch-case statement

		switch(cmd) {
			case 0:  // P1BL
			case 1:  // P2BL
			case 2:  // P1LO
			case 3:  // P2LO
			case 17: // BC_P1BL
			case 18: // BC_P2BL
			case 19: // BC_P1LO
			case 20: // BC_P2LO
				ChecksumInput[0] = cmd;
				ChecksumInput[1] = numObj;
				checksumLoops = 2;
			break;

			case 4:  // TNLM
			case 21: // TNLM
				ChecksumInput[0] = cmd;
				ChecksumInput[1] = tempOrNoise;
				checksumLoops = 2;
			break;

			case 5:  // UMR
			case 6:  // TNLR
			case 7:  // TEDD
			case 8:  // SD
			case 11: // EEBR
			case 13: // TVGBR
			case 15: // THRBR
				ChecksumInput[0] = cmd;
				checksumLoops = 1;
			break;

			case 9: // RR
				ChecksumInput[0] = cmd;
				ChecksumInput[1] = regAddr;
				checksumLoops = 2;
			break;

			case 10: // RW
			case 22: // BC_RW
				ChecksumInput[0] = cmd;
				ChecksumInput[1] = regAddr;
				ChecksumInput[2] = regData;
				checksumLoops = 3;
			break;

			case 14: // TVGBW
			case 24: // BC_TVGBW
				ChecksumInput[0] = cmd;
				ChecksumInput[1] = TVGAIN0;
				ChecksumInput[2] = TVGAIN1;
				ChecksumInput[3] = TVGAIN2;
				ChecksumInput[4] = TVGAIN3;
				ChecksumInput[5] = TVGAIN4;
				ChecksumInput[6] = TVGAIN5;
				ChecksumInput[7] = TVGAIN6;
				checksumLoops = 8;
			break;

			case 16: // THRBW
			case 25: // BC_THRBW
				ChecksumInput[0] = cmd;
				ChecksumInput[1] = P1_THR_0;
				ChecksumInput[2] = P1_THR_1;
				ChecksumInput[3] = P1_THR_2;
				ChecksumInput[4] = P1_THR_3;
				ChecksumInput[5] = P1_THR_4;
				ChecksumInput[6] = P1_THR_5;
				ChecksumInput[7] = P1_THR_6;
				ChecksumInput[8] = P1_THR_7;
				ChecksumInput[9] = P1_THR_8;
				ChecksumInput[10] = P1_THR_9;
				ChecksumInput[11] = P1_THR_10;
				ChecksumInput[12] = P1_THR_11;
				ChecksumInput[13] = P1_THR_12;
				ChecksumInput[14] = P1_THR_13;
				ChecksumInput[15] = P1_THR_14;
				ChecksumInput[16] = P1_THR_15;
				ChecksumInput[17] = P2_THR_0;
				ChecksumInput[18] = P2_THR_1;
				ChecksumInput[19] = P2_THR_2;
				ChecksumInput[20] = P2_THR_3;
				ChecksumInput[21] = P2_THR_4;
				ChecksumInput[22] = P2_THR_5;
				ChecksumInput[23] = P2_THR_6;
				ChecksumInput[24] = P2_THR_7;
				ChecksumInput[25] = P2_THR_8;
				ChecksumInput[26] = P2_THR_9;
				ChecksumInput[27] = P2_THR_10;
				ChecksumInput[28] = P2_THR_11;
				ChecksumInput[29] = P2_THR_12;
				ChecksumInput[30] = P2_THR_13;
				ChecksumInput[31] = P2_THR_14;
				ChecksumInput[32] = P2_THR_15;
				checksumLoops = 33;
			break;

			case 12: // EEBW
			case 23: // BC_EEBW
				ChecksumInput[0] = cmd;
				ChecksumInput[1] = USER_DATA1;
				ChecksumInput[2] = USER_DATA2;
				ChecksumInput[3] = USER_DATA3;
				ChecksumInput[4] = USER_DATA4;
				ChecksumInput[5] = USER_DATA5;
				ChecksumInput[6] = USER_DATA6;
				ChecksumInput[7] = USER_DATA7;
				ChecksumInput[8] = USER_DATA8;
				ChecksumInput[9] = USER_DATA9;
				ChecksumInput[10] = USER_DATA10;
				ChecksumInput[11] = USER_DATA11;
				ChecksumInput[12] = USER_DATA12;
				ChecksumInput[13] = USER_DATA13;
				ChecksumInput[14] = USER_DATA14;
				ChecksumInput[15] = USER_DATA15;
				ChecksumInput[16] = USER_DATA16;
				ChecksumInput[17] = USER_DATA17;
				ChecksumInput[18] = USER_DATA18;
				ChecksumInput[19] = USER_DATA19;
				ChecksumInput[20] = USER_DATA20;
				ChecksumInput[21] = TVGAIN0;
				ChecksumInput[22] = TVGAIN1;
				ChecksumInput[23] = TVGAIN2;
				ChecksumInput[24] = TVGAIN3;
				ChecksumInput[25] = TVGAIN4;
				ChecksumInput[26] = TVGAIN5;
				ChecksumInput[27] = TVGAIN6;
				ChecksumInput[28] = INIT_GAIN;
				ChecksumInput[29] = FREQUENCY;
				ChecksumInput[30] = DEADTIME;
				ChecksumInput[31] = PULSE_P1;
				ChecksumInput[32] = PULSE_P2;
				ChecksumInput[33] = CURR_LIM_P1;
				ChecksumInput[34] = CURR_LIM_P2;
				ChecksumInput[35] = REC_LENGTH;
				ChecksumInput[36] = FREQ_DIAG;
				ChecksumInput[37] = SAT_FDIAG_TH;
				ChecksumInput[38] = FVOLT_DEC;
				ChecksumInput[39] = DECPL_TEMP;
				ChecksumInput[40] = DSP_SCALE;
				ChecksumInput[41] = TEMP_TRIM;
				ChecksumInput[42] = P1_GAIN_CTRL;
				ChecksumInput[43] = P2_GAIN_CTRL;
				checksumLoops = 44;
			break;

			default: break;
		}

		// only re-append command address for non-broadcast commands
		if(ChecksumInput[0] < 17) {
			ChecksumInput[0] = ChecksumInput[0] + (uartAddr << 5);
		}

		uint16_t carry = 0;
		for(int i = 0; i < checksumLoops; i++) {
			if((ChecksumInput[i] + carry) < carry) {
				carry = carry + ChecksumInput[i] + 1;
			} else {
				carry = carry + ChecksumInput[i];
			}

			if(carry > 0xFF) {
				carry = carry - 255;
			}
		}

		carry = (~carry & 0x00FF);

		return carry;
	}





	/*
		Uses the law of cosines to compute the position of the targeted object from transceiver S1.

		distanceA (IN) -- distance (m) from sensor module 1 (S1) to the targeted object based on UMR result
		distanceB (IN) -- distance (m) between sensor module 1 (S1) and sensor module 2 (S2)
		distanceC (IN) -- distance (m) from sensor module 2 (S2) to the targeted object based on UMR result

		Returns:  angle (degrees) from transceiver element S1 to the targeted object
	*/
	double Triangulation(double a, double b, double c) {
		// LAW OF COSINES

		if(a + b > c) {
			// Radian to Degree = Rad * (180/PI)
			double inAngle = (acos(((a * a) + (b * b) - (c * c)) / (2 * a * b))) * 57.3;
			return inAngle;
		} else {
			return 360;
		}

		// COORDINATE
		// TODO
	}





	/*
		addr (IN) -- PGA460 register address to read data from
	*/
	uint8 RegisterRead(uint8 addr) {
		uint8 data = 0x00;

		if(comm == 2) {
			owuShift = 1; // OWU receive buffer offset to ignore transmitted data
		} else {
			owuShift = 0;
		}

		regAddr = addr;
		uint8 buf9[4] = { syncByte, SRR, regAddr, CalcChecksum(SRR) };

		// UART or OWU mode
		SerialWrite((uint8*)buf9, sizeof(buf9));
		WaitWrite();
		System::DelayMs(10);


		uint8 buf3[3];
		SerialRead((uint8*)buf3, 3);
		WaitRead();
		data = buf3[1 - owuShift]; // store read data

		return data;
	}





	/*
		addr (IN) -- PGA460 register address to write data to
		data (IN) -- 8-bit data value to write into register
	*/
	void RegisterWrite(uint8 addr, uint8 data) {
		regAddr = addr;
		regData = data;
		uint8 buf10[5] = { syncByte, SRW, regAddr, regData, CalcChecksum(SRW) };
		SerialWrite((uint8*)buf10, sizeof(buf10));
		WaitWrite();
		System::DelayMs(10);
	}





	/*
		Automatically assigns threshold time and level values based on a no-object burst/listen command

		cmd (IN) -- preset 1 or 2 burst and/or listen command to run
		noiseMargin (IN) -- margin between maximum downsampled noise
							value and the threshold level in intervals
							of 8.

		windowIndex (IN) -- spacing between each threshold time as an
							index (refer to datasheet for microsecond
							equivalent). To use the existing threshold
							times, enter a value of '16'.

		autoMax (IN) -- 	automatically set threshold levels up to this
							threshold point (maximum is 12). Remaining levels
							will not change.

		loops (IN) -- 		number of command loops to run to collect a
							running average of the echo data dump points.
	*/
	void AutoThreshold(uint8 cmd, uint8 noiseMargin, uint8 windowIndex, uint8 autoMax, uint8 avgLoops) {
		// local variables
		uint8 thrTime[6]; // threshold time values for selected preset
		uint8 thrLevel[10]; //threshold level values for selected preset
		uint8 thrMax[12]; // maximum echo data dump values per partition
		uint8 presetOffset = 0; // determines if regsiter threshold address space is initialized at P1 or P2
		uint8 thrOffset = 0; // -6 to +7 where MSB is sign value
		bool thrOffsetFlag = 0; //when high, the level offset value is updated

		// read existing threhsold values into thrTime array
		switch(cmd) {

			// Preset 1 command
			case 0:
			case 2:
				ThresholdBulkRead(1);
			break;

			// Preset 2 command
			case 1:
			case 3:
				ThresholdBulkRead(2);
				presetOffset = 16;
			break;

			// Invalid command
			default: return;
		}

		// set thrTime and thrLevel to existing threshold time and level values respectively
		for(uint8 h = 0; h < 6; h++) {
			thrTime[h] = bulkThr[h + presetOffset];
		}
		for(uint8 g = 0; g < 10; g++) {
			thrLevel[g] = bulkThr[g + 6 + presetOffset];
		}

		// replace each preset time with windowIndex for the number of points to auto-calc
		if(windowIndex >= 16) {
			// skip threshold-time configuration
		} else {
			for(uint8 i = 0; i < 12; i += 2) {
				if(autoMax > i) {
					thrTime[i / 2] = thrTime[i / 2] & 0x0F;
					thrTime[i / 2] = (windowIndex << 4) | thrTime[i / 2];
					if(autoMax > i + 1) {
						thrTime[i / 2] = thrTime[i / 2] & 0xF0;
						thrTime[i / 2] = (windowIndex & 0x0F) | thrTime[i / 2];
					}
				}
			}
		}

		// run burst-and-listen to collect EDD data
		RunEchoDataDump(cmd);

		// read the record length value for the preset
		uint8 recLength = RegisterRead(0x22); // read REC_LENGTH Register
		switch (cmd) {

			// Preset 1 command
			case 0:
			case 2:
				recLength = (recLength >> 4) & 0x0F;
			break;

			// Preset 2 command
			case 1:
			case 3:
				recLength = recLength & 0x0F;
			break;

			// Invalid command
			default: return;
		}

		// convert record length value to time equivalent in microseconds
		unsigned int recTime = (recLength + 1) * 4096;

		//determine the number of threshold points that are within the record length time
		uint8 numPoints = 0;
		uint8 thrTimeReg = 0;
		unsigned int thrMicro = 0; // threhsold total time in microseconds
		unsigned int eddMarker[12]; // echo data dump time marker between each threhsold point

		for(thrTimeReg = 0; thrTimeReg < 6; thrTimeReg++) {

			// check threshold 1 of 2 in single register
			switch((thrTime[thrTimeReg] >> 4) & 0x0F) {
				case 0:  thrMicro += 100; break;
				case 1:  thrMicro += 200; break;
				case 2:  thrMicro += 300; break;
				case 3:  thrMicro += 400; break;
				case 4:  thrMicro += 600; break;
				case 5:  thrMicro += 800; break;
				case 6:  thrMicro += 1000; break;
				case 7:  thrMicro += 1200; break;
				case 8:  thrMicro += 1400; break;
				case 9:  thrMicro += 2000; break;
				case 10: thrMicro += 2400; break;
				case 11: thrMicro += 3200; break;
				case 12: thrMicro += 4000; break;
				case 13: thrMicro += 5200; break;
				case 14: thrMicro += 6400; break;
				case 15: thrMicro += 8000; break;
				default: break;
			}

			eddMarker[thrTimeReg * 2] = thrMicro;

			if(thrMicro >= recTime) {
				numPoints = thrTimeReg * 2;
				thrTimeReg = 6; // exit
			} else {

				// check threshold 2 of 2 in single register
				switch (thrTime[thrTimeReg] & 0x0F) {
					case 0:  thrMicro += 100; break;
					case 1:  thrMicro += 200; break;
					case 2:  thrMicro += 300; break;
					case 3:  thrMicro += 400; break;
					case 4:  thrMicro += 600; break;
					case 5:  thrMicro += 800; break;
					case 6:  thrMicro += 1000; break;
					case 7:  thrMicro += 1200; break;
					case 8:  thrMicro += 1400; break;
					case 9:  thrMicro += 2000; break;
					case 10: thrMicro += 2400; break;
					case 11: thrMicro += 3200; break;
					case 12: thrMicro += 4000; break;
					case 13: thrMicro += 5200; break;
					case 14: thrMicro += 6400; break;
					case 15: thrMicro += 8000; break;
					default: break;
				}

				eddMarker[thrTimeReg * 2 + 1] = thrMicro;

				if(thrMicro >= recTime) {
					numPoints = (thrTimeReg * 2) + 1;
					thrTimeReg = 6; // exit
				}
			}
		}

		// if all points fall within the record length
		if(numPoints == 0) {
			numPoints = 11;
		}

		// convert up to 12 echo data dump markers from microseconds to index
		uint8 eddIndex[13];
		eddIndex[0] = 0;
		for(uint8 l = 0; l < 12; l++) {
			eddIndex[l + 1] = ((eddMarker[l] / 100) * 128) / (recTime / 100); // divide by 100 for best accuracy in MSP430
		}

		// downsample the echo data dump based on the number of partitions
		memset(thrMax, 0x00, 12); // zero thrMax array
		uint8 eddLevel = 0;
		for(uint8 j = 0; j < numPoints + 1; j++) {
			eddLevel = 0;
			for(uint8 k = eddIndex[j]; k < eddIndex[j + 1]; k++) {
				eddLevel = PullEchoDataDump(k);
				if(thrMax[j] < eddLevel) {
					thrMax[j] = eddLevel;
				}
			}
		}

		// set threhsold points which exceed the record length to same value as last valid value
		if(numPoints < autoMax) {
			for(int o = numPoints; o < autoMax; o++) {
				if(numPoints == 0) {
					thrMax[o] = 128;
				} else {
					thrMax[o] = thrMax[numPoints - 1];
				}
			}
		}

		// filter y-max for level compatibility of first eight points
		for(uint8 m = 0; m < 8; m++) {

			// first eight levels must be mutliples of eight
			while((thrMax[m] % 8 != 0) && (thrMax[m] < 248)) {
				thrMax[m] += 1;
			}
		}

		// apply noise floor offset
		for(uint8 n = 0; n < 12; n++) {
			if(thrMax[n] + noiseMargin >= 248 && thrMax[n] + noiseMargin < 255) {
				thrMax[n] = 248;
				thrOffset = 0b0110; // +6
				thrOffsetFlag = true;
			} else if(thrMax[n] + noiseMargin >= 255) {
				thrMax[n] = 248;
				thrOffset = 0b0111; // +7
				thrOffsetFlag = true;
			} else {
				thrMax[n] += noiseMargin;
			}
		}

		// convert first eight auto calibrated levels to five-bit equivalents
		uint8 rounding = 0;
		if(autoMax >= 8) {
			rounding = 8;
		} else {
			rounding = autoMax;
		}

		for(uint8 p = 0; p < rounding; p++) {
			thrMax[p] = thrMax[p] / 8;
		}

		// ---- concatenate and merge threshold level register values
		// Px_THR_6 L1,L2
		if(autoMax > 0) {
			thrLevel[0] = (thrLevel[0] & ~0xF8) | (thrMax[0] << 3);
		}

		// Px_THR_6 L1,L2
		if(autoMax > 1) {
			thrLevel[0] = (thrLevel[0] & ~0x07) | (thrMax[1] >> 2);
		}

		// Px_THR_7 L2,L3,L4
		if(autoMax > 1) {
			thrLevel[1] = (thrLevel[1] & ~0xC0) | (thrMax[1] << 6);
		}

		// Px_THR_7 L2,L3,L4
		if(autoMax > 2) {
			thrLevel[1] = (thrLevel[1] & ~0x3E) | (thrMax[2] << 1);
		}

		// Px_THR_7 L2,L3,L4
		if(autoMax > 3) {
			thrLevel[1] = (thrLevel[1] & ~0x01) | (thrMax[3] >> 4);
		}

		// Px_THR_8 L4,L5
		if(autoMax > 3) {
			thrLevel[2] = (thrLevel[2] & ~0xF0) | (thrMax[3] << 4);
		}

		// Px_THR_8 L4,L5
		if(autoMax > 4) {
			thrLevel[2] = (thrLevel[2] & ~0x0F) | (thrMax[4] >> 1);
		}

		// Px_THR_9 L5,L6,L7
		if(autoMax > 4) {
			thrLevel[3] = (thrLevel[3] & ~0x80) | (thrMax[4] << 7);
		}

		// Px_THR_9 L5,L6,L7
		if(autoMax > 5) {
			thrLevel[3] = (thrLevel[3] & ~0x7C) | (thrMax[5] << 2);
		}

		// Px_THR_9 L5,L6,L7
		if(autoMax > 6) {
			thrLevel[3] = (thrLevel[3] & ~0x03) | (thrMax[6] >> 3);
		}

		// Px_THR_10 L7,L8
		if(autoMax > 6) {
			thrLevel[4] = (thrLevel[4] & ~0xE0) | (thrMax[6] << 5);
		}

		// Px_THR_10 L7,L8
		if(autoMax > 7) {
			thrLevel[4] = (thrLevel[4] & ~0x1F) | (thrMax[7]);
		}

		// Px_THR_11 L9
		if(autoMax > 8) {
			thrLevel[5] = thrMax[8];
		}

		// Px_THR_12 L10
		if(autoMax > 9) {
			thrLevel[6] = thrMax[9];
		}

		// Px_THR_13 L11
		if(autoMax > 10) {
			thrLevel[7] = thrMax[10];
		}

		// Px_THR_14 L12
		if(autoMax > 11) {
			thrLevel[8] = thrMax[11];
		}

		// Px_THR_15 LOff
		if(thrOffsetFlag == true) {
			thrLevel[9] = thrOffset & 0x0F;
		}

		// update threshold register values
		switch(cmd) {

			// Preset 1 command
			case 0:
			case 2:
				P1_THR_0 = thrTime[0];
				P1_THR_1 = thrTime[1];
				P1_THR_2 = thrTime[2];
				P1_THR_3 = thrTime[3];
				P1_THR_4 = thrTime[4];
				P1_THR_5 = thrTime[5];
				P1_THR_6 = thrLevel[0];
				P1_THR_7 = thrLevel[1];
				P1_THR_8 = thrLevel[2];
				P1_THR_9 = thrLevel[3];
				P1_THR_10 = thrLevel[4];
				P1_THR_11 = thrLevel[5];
				P1_THR_12 = thrLevel[6];
				P1_THR_13 = thrLevel[7];
				P1_THR_14 = thrLevel[8];
				P1_THR_15 = thrLevel[9];

				ThresholdBulkRead(2);
				presetOffset = 16;

				P2_THR_0 = bulkThr[0 + presetOffset];
				P2_THR_1 = bulkThr[1 + presetOffset];
				P2_THR_2 = bulkThr[2 + presetOffset];
				P2_THR_3 = bulkThr[3 + presetOffset];
				P2_THR_4 = bulkThr[4 + presetOffset];
				P2_THR_5 = bulkThr[5 + presetOffset];
				P2_THR_6 = bulkThr[6 + presetOffset];
				P2_THR_7 = bulkThr[7 + presetOffset];
				P2_THR_8 = bulkThr[8 + presetOffset];
				P2_THR_9 = bulkThr[9 + presetOffset];
				P2_THR_10 = bulkThr[10 + presetOffset];
				P2_THR_11 = bulkThr[11 + presetOffset];
				P2_THR_12 = bulkThr[12 + presetOffset];
				P2_THR_13 = bulkThr[13 + presetOffset];
				P2_THR_14 = bulkThr[14 + presetOffset];
				P2_THR_15 = bulkThr[15 + presetOffset];
			break;

			// Preset 2 command
			case 1:
			case 3:
				P2_THR_0 = thrTime[0];
				P2_THR_1 = thrTime[1];
				P2_THR_2 = thrTime[2];
				P2_THR_3 = thrTime[3];
				P2_THR_4 = thrTime[4];
				P2_THR_5 = thrTime[5];
				P2_THR_6 = thrLevel[0];
				P2_THR_7 = thrLevel[1];
				P2_THR_8 = thrLevel[2];
				P2_THR_9 = thrLevel[3];
				P2_THR_10 = thrLevel[4];
				P2_THR_11 = thrLevel[5];
				P2_THR_12 = thrLevel[6];
				P2_THR_13 = thrLevel[7];
				P2_THR_14 = thrLevel[8];
				P2_THR_15 = thrLevel[9];

				ThresholdBulkRead(1);
				presetOffset = 0;

				P1_THR_0 = bulkThr[0 + presetOffset];
				P1_THR_1 = bulkThr[1 + presetOffset];
				P1_THR_2 = bulkThr[2 + presetOffset];
				P1_THR_3 = bulkThr[3 + presetOffset];
				P1_THR_4 = bulkThr[4 + presetOffset];
				P1_THR_5 = bulkThr[5 + presetOffset];
				P1_THR_6 = bulkThr[6 + presetOffset];
				P1_THR_7 = bulkThr[7 + presetOffset];
				P1_THR_8 = bulkThr[8 + presetOffset];
				P1_THR_9 = bulkThr[9 + presetOffset];
				P1_THR_10 = bulkThr[10 + presetOffset];
				P1_THR_11 = bulkThr[11 + presetOffset];
				P1_THR_12 = bulkThr[12 + presetOffset];
				P1_THR_13 = bulkThr[13 + presetOffset];
				P1_THR_14 = bulkThr[14 + presetOffset];
				P1_THR_15 = bulkThr[15 + presetOffset];
			break;

			// Invalid command
			default: return;
		}

		uint8 p1ThrMap[16] = {
			P1_THR_0, P1_THR_1, P1_THR_2, P1_THR_3, P1_THR_4, P1_THR_5,
			P1_THR_6, P1_THR_7, P1_THR_8, P1_THR_9, P1_THR_10, P1_THR_11,
			P1_THR_12, P1_THR_13, P1_THR_14, P1_THR_15
		};
		uint8 p2ThrMap[16] = {
			P2_THR_0, P2_THR_1, P2_THR_2, P2_THR_3, P2_THR_4, P2_THR_5,
			P2_THR_6, P2_THR_7, P2_THR_8, P2_THR_9, P2_THR_10, P2_THR_11,
			P2_THR_12, P2_THR_13, P2_THR_14, P2_THR_15
		};

		ThresholdBulkWrite(p1ThrMap, p2ThrMap);
	}





	/*
		Bulk read all threshold times and levels
		preset (IN) -- which preset's threshold data to read
	*/
	void ThresholdBulkRead(uint8 preset) {
		uint8 presetOffset = 0;
		uint8 addr = 0x5F; // beginning of threshold register space


		// Preset 2 advances 16 address uint8s
		if(preset == 2) {
			presetOffset = 16;
		}

		for(uint8 n = 0; n < 16; n++) {
			bulkThr[n + presetOffset] = RegisterRead(addr + presetOffset);
			addr++;
		}
	}





	/*
		Bulk write to all threshold registers

		p1ThrMap (IN) -- data uint8 array for 16 uint8s of Preset 1 threhsold data
		p2ThrMap (IN) -- data uint8 array for 16 uint8s of Preset 2 threhsold data
	*/
	void ThresholdBulkWrite(uint8* p1ThrMap, uint8* p2ThrMap) {
		uint8 buf16[35] = {
			syncByte, THRBW, p1ThrMap[0], p1ThrMap[1], p1ThrMap[2], p1ThrMap[3], p1ThrMap[4], p1ThrMap[5],
			p1ThrMap[6], p1ThrMap[7], p1ThrMap[8], p1ThrMap[9], p1ThrMap[10], p1ThrMap[11], p1ThrMap[12],
			p1ThrMap[13], p1ThrMap[14], p1ThrMap[15],
			p2ThrMap[0], p2ThrMap[1], p2ThrMap[2], p2ThrMap[3], p2ThrMap[4], p2ThrMap[5],
			p2ThrMap[6], p2ThrMap[7], p2ThrMap[8], p2ThrMap[9], p2ThrMap[10], p2ThrMap[11], p2ThrMap[12],
			p2ThrMap[13], p2ThrMap[14], p2ThrMap[15],
			CalcChecksum(THRBW)
		};
		SerialWrite((uint8*)buf16, sizeof(buf16)); // serial transmit master data for bulk threhsold
		WaitWrite();
		System::DelayMs(100);
	}






	/*
		Copy a single preset's threshold times and levels to USER_DATA1-16 in EEPROM

		preset (IN) -- preset's threshold to copy
		saveLoad (IN) -- when false, copy threshold to EEPROM;when true, copy threshold from EEPROM
	*/
	void EepromThreshold(uint8 preset, bool saveLoad) {
		uint8 presetOffset = 0;
		uint8 addr = 0x5F; // beginning of threshold memory space

		// save thr
		if(saveLoad == false) {

			// Preset 2 advances 16 address uint8s
			if(preset == 2 || preset == 4) {
				presetOffset = 16;
			}

			for(uint8 n = 0; n < 16; n++) {
				bulkThr[n + presetOffset] = RegisterRead(addr + presetOffset);

				// write threshold values into USER_DATA1-16
				RegisterWrite(n, bulkThr[n + presetOffset]);
				addr++;
			}
		} else { // load thr

			// Preset 2 advances 16 address uint8s
			if(preset == 2 || preset == 4) {
				presetOffset = 16;
			}

			// copy USER_DATA1-16 into selected preset threhsold space
			for(uint8 n = 0; n < 16; n++) {
				bulkThr[n + presetOffset] = RegisterRead(n);

				// bulk write to threshold
				RegisterWrite(addr + presetOffset, bulkThr[n + presetOffset]);
				addr++;
			}
		}
	}





private:
	uint8 CalcChecksum(uint8 cmd);

	bool inline SerialWrite(uint8 *buffer, size_t size) {
		return serial->WriteArrayAsync(buffer, size) == Status::ok;
	}

	bool inline SerialRead(uint8 *buffer, size_t size) {
		return serial->ReadArrayAsync(buffer, size) == Status::ok;
	}

	void WaitWrite() {
		while(serial->GetTxState() != Status::ready);
	}

	void WaitRead() {
		while(serial->GetRxState() != Status::ready);
	}
};











