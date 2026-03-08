/**************************************************************************//**
 * @file     CMSDK_CM0.h
 * @brief    CMSIS Cortex-M0 Core Peripheral Access Layer Header File for
 *           Device CMSDK
 * @version  V3.01
 * @date     06. March 2012
 *
 * @note
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 *
 * @par
 * ARM Limited (ARM) is supplying this software for use with Cortex-M
 * processor based microcontrollers.  This file can be freely distributed
 * within development tools that are supporting such ARM based processors.
 *
 * @par
 * THIS SOFTWARE IS PROVIDED "AS IS".  NO WARRANTIES, WHETHER EXPRESS, IMPLIED
 * OR STATUTORY, INCLUDING, BUT NOT LIMITED TO, IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE APPLY TO THIS SOFTWARE.
 * ARM SHALL NOT, IN ANY CIRCUMSTANCES, BE LIABLE FOR SPECIAL, INCIDENTAL, OR
 * CONSEQUENTIAL DAMAGES, FOR ANY REASON WHATSOEVER.
 *
 ******************************************************************************/


#ifndef CMSDK_H
#define CMSDK_H

#ifdef __cplusplus
 extern "C" {
#endif

/** @addtogroup CMSDK_Definitions CMSDK Definitions
  This file defines all structures and symbols for CMSDK:
    - registers and bitfields
    - peripheral base address
    - peripheral ID
    - Peripheral definitions
  @{
*/


/******************************************************************************/
/*                Processor and Core Peripherals                              */
/******************************************************************************/
/** @addtogroup CMSDK_CMSIS Device CMSIS Definitions
  Configuration of the Cortex-M0 Processor and Core Peripherals
  @{
*/

/*
 * ==========================================================================
 * ---------- Interrupt Number Definition -----------------------------------
 * ==========================================================================
 */

typedef enum IRQn
{
/******  Cortex-M0 Processor Exceptions Numbers ***************************************************/

/* ToDo: use this Cortex interrupt numbers if your device is a CORTEX-M0 device                   */
  NonMaskableInt_IRQn           = -14,      /*!<  2 Cortex-M0 Non Maskable Interrupt              */
  HardFault_IRQn                = -13,      /*!<  3 Cortex-M0 Hard Fault Interrupt                */
  SVCall_IRQn                   = -5,       /*!< 11 Cortex-M0 SV Call Interrupt                   */
  PendSV_IRQn                   = -2,       /*!< 14 Cortex-M0 Pend SV Interrupt                   */
  SysTick_IRQn                  = -1,       /*!< 15 Cortex-M0 System Tick Interrupt               */

/******  CMSDK Specific Interrupt Numbers *********************************************************/
  LVD_IRQn                      = 0,       
  RTC_IRQn                      = 1,       
  COMP0_IRQn                    = 2,       
  COMP1_IRQn                    = 3,       
  GPIO0_7_IRQn                  = 4,       
  GPIO8_15_IRQn                 = 5,       
  GPIO16_23_IRQn                = 6,       
  MTP_IRQn                      = 7,       
  CHARGER_OK_IRQn               = 8,       
  CHARGER_END_IRQn              = 9,       
  ADC_IRQn                      = 10,      
  LCD_IRQn                      = 11,      
  UART0_IRQn                    = 12,      
  UART1_IRQn                    = 13,      
  SPI0_IRQn                     = 14,      
  SPI1_IRQn                     = 15,      
  I2C0_Event_IRQn               = 16,      
  I2C0_Error_IRQn               = 17,
  I2C1_Event_IRQn               = 18,
  I2C1_Error_IRQn               = 19,
  PWM_IRQn                      = 20,
  TIMER0_IRQn                   = 21,
  TIMER1_IRQn                   = 22,
  DUALTIMER_IRQn                = 23,
  OVER_TEMP_IRQn                = 24,
  WG_DRV_IRQn       		= 25,
} IRQn_Type;


/*
 * ==========================================================================
 * ----------- Processor and Core Peripheral Section ------------------------
 * ==========================================================================
 */

/* Configuration of the Cortex-M0 Processor and Core Peripherals */
#define __CM0_REV                 0x0000    /*!< Core Revision r0p0                               */
#define __NVIC_PRIO_BITS          2         /*!< Number of Bits used for Priority Levels          */
#define __Vendor_SysTickConfig    0         /*!< Set to 1 if different SysTick Config is used     */
#define __MPU_PRESENT             0         /*!< MPU present or not                               */

/*@}*/ /* end of group CMSDK_CMSIS */


#include "core_cm0.h"                       /* Cortex-M0 processor and core peripherals           */
#include "system_CMSDK_CM0.h"               /* CMSDK System include file                          */


/******************************************************************************/
/*                Device Specific Peripheral registers structures             */
/******************************************************************************/
/** @addtogroup CMSDK_Peripherals CMSDK Peripherals
  CMSDK Device Specific Peripheral registers structures
  @{
*/

#if defined ( __CC_ARM   )
#pragma anon_unions
#endif

/*------------- Universal Asynchronous Receiver Transmitter (UART) -----------*/
/** @addtogroup CMSDK_UART Universal Asynchronous Receiver/Transmitter
  memory mapped structure for CMSDK_UART
  @{
*/
typedef struct
{
  union {
    __I    uint32_t  RBR;	    /*!< Offset: 0x000 Receiver Buffer Register (R/) */
    __O    uint32_t  THR;           /*!< Offset: 0x000 Transmitter Holding Register ( /W) */
    };
  __IO   uint32_t  IER;    	    /*!< Offset: 0x004 Interrupt Enable Register (R/W) */
  union {
    __I    uint32_t  IIR;     	    /*!< Offset: 0x008 Interrupt Identification Register (R/) */
    __O    uint32_t  FCR;      	    /*!< Offset: 0x008 FIFO Control Register ( /W) */
    };
  __IO   uint32_t  LCR;             /*!< Offset: 0x00C Line Control Register  (R/W) */
  __IO   uint32_t  MCR;             /*!< Offset: 0x010 Modem Control Register (R/W) */
  __I    uint32_t  LSR;             /*!< Offset: 0x014 Line Status Register  (R/) */
  __I    uint32_t  MSR;             /*!< Offset: 0x018 Modem Status Register (R/) */
  __IO   uint32_t  SCR;             /*!< Offset: 0x01C Scratch Pad Register (R/W) */
  __IO   uint32_t  DLL;             /*!< Offset: 0x020 Divisor LSB Latch Register (R/W) */
  __IO   uint32_t  DLH;             /*!< Offset: 0x024 Divisor MSB Latch Register (R/W) */
  __I    uint32_t  FSR;             /*!< Offset: 0x028 FIFO Status Register (R/) */
  __I    uint32_t  DBG;             /*!< Offset: 0x02C Debug Signal Register (R/) */
  __IO   uint32_t  PMU;             /*!< Offset: 0x030 Power Management Register (R/W) */
  __IO   uint32_t  MDR;             /*!< Offset: 0x034 Mode Definition Register (R/W) */ 

} CMSDK_UART_TypeDef;

/* CMSDK_UART Register Definitions */
#define CMSDK_UART_IER_RLSI_EN_Pos         	2                                            	/*!< CMSDK_UART IER: RLSI_EN Position */
#define CMSDK_UART_IER_RLSI_EN_Msk         	(0x01ul << CMSDK_UART_IER_RLSI_EN_Pos)        	/*!< CMSDK_UART IER: RLSI_EN Mask */

#define CMSDK_UART_IER_THRE_EN_Pos         	1                                             	/*!< CMSDK_UART IER: THRE_EN Position */
#define CMSDK_UART_IER_THRE_EN_Msk         	(0x01ul << CMSDK_UART_IER_THRE_EN_Pos)        	/*!< CMSDK_UART IER: THRE_EN Mask */

#define CMSDK_UART_IER_RDAI_EN_Pos         	0                                             	/*!< CMSDK_UART IER: RDAI_EN Position */
#define CMSDK_UART_IER_RDAI_EN_Msk         	(0x01ul << CMSDK_UART_IER_RDAI_EN_Pos)        	/*!< CMSDK_UART IER: RDAI_EN Mask */

#define CMSDK_UART_IIR_INT_TYPE_Pos         	1                                             	/*!< CMSDK_UART IIR: INT_TYPE Position */
#define CMSDK_UART_IIR_INT_TYPE_Msk         	(0x07ul << CMSDK_UART_IIR_INT_TYPE_Pos)        	/*!< CMSDK_UART IIR: INT_TYPE Mask */

#define CMSDK_UART_FCR_RX_FIFO_TL_Pos         	6                                             	/*!< CMSDK_UART FCR: RX_FIFO_TL Position */
#define CMSDK_UART_FCR_RX_FIFO_TL_Msk         	(0x03ul << CMSDK_UART_FCR_RX_FIFO_TL_Pos)       /*!< CMSDK_UART FCR: RX_FIFO_TL Mask */

#define CMSDK_UART_FCR_DMA_EN_Pos         	3                                             	/*!< CMSDK_UART FCR: DMA_EN Position */
#define CMSDK_UART_FCR_DMA_EN_Msk         	(0x01ul << CMSDK_UART_FCR_DMA_EN_Pos)        	/*!< CMSDK_UART FCR: DMA_EN Mask */

#define CMSDK_UART_FCR_FIFO_EN_Pos         	0                                             	/*!< CMSDK_UART FCR: FIFO_EN Position */
#define CMSDK_UART_FCR_FIFO_EN_Msk         	(0x01ul << CMSDK_UART_FCR_FIFO_EN_Pos)        	/*!< CMSDK_UART FCR: FIFO_EN Mask */

#define CMSDK_UART_LCR_BREAK_EN_Pos         	6                                             	/*!< CMSDK_UART LCR: BREAK_EN Position */
#define CMSDK_UART_LCR_BREAK_EN_Msk         	(0x01ul << CMSDK_UART_LCR_BREAK_EN_Pos)        	/*!< CMSDK_UART LCR: BREAK_EN Mask */

#define CMSDK_UART_LCR_STICK_EN_Pos         	5                                             	/*!< CMSDK_UART LCR: STICK_EN Position */
#define CMSDK_UART_LCR_STICK_EN_Msk         	(0x01ul << CMSDK_UART_LCR_STICK_EN_Pos)        	/*!< CMSDK_UART LCR: STICK_EN Mask */

#define CMSDK_UART_LCR_EVEN_EN_Pos         	4                                             	/*!< CMSDK_UART LCR: EVEN_EN Position */
#define CMSDK_UART_LCR_EVEN_EN_Msk         	(0x01ul << CMSDK_UART_LCR_EVEN_EN_Pos)        	/*!< CMSDK_UART LCR: EVEN_EN Mask */

#define CMSDK_UART_LCR_PARITY_EN_Pos         	3                                             	/*!< CMSDK_UART LCR: PARITY_EN Position */
#define CMSDK_UART_LCR_PARITY_EN_Msk         	(0x01ul << CMSDK_UART_LCR_PARITY_EN_Pos)        /*!< CMSDK_UART LCR: PARITY_EN Mask */

#define CMSDK_UART_LCR_STOP_LEN_Pos         	2                                             	/*!< CMSDK_UART LCR: STOP_LEN Position */
#define CMSDK_UART_LCR_STOP_LEN_Msk         	(0x01ul << CMSDK_UART_LCR_STOP_LEN_Pos)        	/*!< CMSDK_UART LCR: STOP_LEN Mask */

#define CMSDK_UART_LCR_WORD_LEN_Pos         	0                                             	/*!< CMSDK_UART LCR: WORD_LEN Position */
#define CMSDK_UART_LCR_WORD_LEN_Msk         	(0x03ul << CMSDK_UART_LCR_WORD_LEN_Pos)        	/*!< CMSDK_UART LCR: WORD_LEN Mask */

#define CMSDK_UART_LSR_RX_ERR_STS_Pos         	7                                             	/*!< CMSDK_UART LSR: RX_ERR_STS Position */
#define CMSDK_UART_LSR_RX_ERR_STS_Msk         	(0x01ul << CMSDK_UART_LSR_RX_ERR_STS_Pos)      	/*!< CMSDK_UART LSR: RX_ERR_STS Mask */

#define CMSDK_UART_LSR_TX_EMPTY_Pos         	6                                             	/*!< CMSDK_UART LSR: TRANSMITTER_EMPTY Position */
#define CMSDK_UART_LSR_TX_EMPTY_Msk         	(0x01ul << CMSDK_UART_LSR_TX_EMPTY_Pos)        	/*!< CMSDK_UART LSR: TRANSMITTER_EMPTY Mask */

#define CMSDK_UART_LSR_THR_EMPTY_Pos         	5                                             	/*!< CMSDK_UART LSR: THR_EMPTY Position */
#define CMSDK_UART_LSR_THR_EMPTY_Msk         	(0x01ul << CMSDK_UART_LSR_THR_EMPTY_Pos)        /*!< CMSDK_UART LSR: THR_EMPTY Mask */

#define CMSDK_UART_LSR_BREAK_ERR_STS_Pos        4                                             	/*!< CMSDK_UART LSR: BREAK_ERR_STS Position */
#define CMSDK_UART_LSR_BREAK_ERR_STS_Msk        (0x01ul << CMSDK_UART_LSR_BREAK_ERR_STS_Pos)    /*!< CMSDK_UART LSR: BREAK_ERR_STS Mask */

#define CMSDK_UART_LSR_FRAME_ERR_STS_Pos        3                                             	/*!< CMSDK_UART LSR: FRAME_ERR_STS Position */
#define CMSDK_UART_LSR_FRAME_ERR_STS_Msk        (0x01ul << CMSDK_UART_LSR_FRAME_ERR_STS_Pos)    /*!< CMSDK_UART LSR: FRAME_ERR_STS Mask */

#define CMSDK_UART_LSR_PARITY_ERR_STS_Pos       2                                             	/*!< CMSDK_UART LSR: PARITY_ERR_STS Position */
#define CMSDK_UART_LSR_PARITY_ERR_STS_Msk       (0x01ul << CMSDK_UART_LSR_PARITY_ERR_STS_Pos)   /*!< CMSDK_UART LSR: PARITY_ERR_STS Mask */

#define CMSDK_UART_LSR_OVRRUN_ERR_STS_Pos       1                                             	/*!< CMSDK_UART LSR: OVRRUN_ERR_STS Position */
#define CMSDK_UART_LSR_OVRRUN_ERR_STS_Msk       (0x01ul << CMSDK_UART_LSR_OVRRUN_ERR_STS_Pos)   /*!< CMSDK_UART LSR: OVRRUN_ERR_STS Mask */

#define CMSDK_UART_MCR_RTS_TRI_MODE_Pos         6                                             	/*!< CMSDK_UART MCR: RTS_TRI_MODE Position */
#define CMSDK_UART_MCR_RTS_TRI_MODE_Msk         (0x01ul << CMSDK_UART_MCR_RTS_TRI_MODE_Pos)     /*!< CMSDK_UART MCR: RTS_TRI_MODE Mask */

#define CMSDK_UART_MCR_AUTOFLOW_EN_Pos         	5                                             	/*!< CMSDK_UART MCR: AUTOFLOW_EN Position */
#define CMSDK_UART_MCR_AUTOFLOW_EN_Msk         	(0x01ul << CMSDK_UART_MCR_AUTOFLOW_EN_Pos)      /*!< CMSDK_UART MCR: AUTOFLOW_EN Mask */

#define CMSDK_UART_MCR_LOOPBACK_EN_Pos         	4                                             	/*!< CMSDK_UART MCR: LOOPBACK_EN Position */
#define CMSDK_UART_MCR_LOOPBACK_EN_Msk         	(0x01ul << CMSDK_UART_MCR_LOOPBACK_EN_Pos)      /*!< CMSDK_UART MCR: LOOPBACK_EN Mask */

#define CMSDK_UART_MCR_AUX2_Pos         	3                                             	/*!< CMSDK_UART MCR: AUX2 Position */
#define CMSDK_UART_MCR_AUX2_Msk         	(0x01ul << CMSDK_UART_MCR_AUX2_Pos)        	/*!< CMSDK_UART MCR: AUX2 Mask */

#define CMSDK_UART_MCR_AUX1_Pos         	2                                             	/*!< CMSDK_UART MCR: AUX1 Position */
#define CMSDK_UART_MCR_AUX1_Msk         	(0x01ul << CMSDK_UART_MCR_AUX1_Pos)        	/*!< CMSDK_UART MCR: AUX1 Mask */

#define CMSDK_UART_MCR_RTS_CTRL_Pos         	1                                             	/*!< CMSDK_UART MCR: RTS_CTRL Position */
#define CMSDK_UART_MCR_RTS_CTRL_Msk         	(0x01ul << CMSDK_UART_MCR_RTS_CTRL_Pos)        	/*!< CMSDK_UART MCR: RTS_CTRL Mask */

#define CMSDK_UART_MCR_DTR_CTRL_Pos         	0                                             	/*!< CMSDK_UART MCR: DTR_CTRL Position */
#define CMSDK_UART_MCR_DTR_CTRL_Msk         	(0x01ul << CMSDK_UART_MCR_DTR_CTRL_Pos)        	/*!< CMSDK_UART MCR: DTR_CTRL Mask */

#define CMSDK_UART_PMU_TXRST_Pos         	1                                             	/*!< CMSDK_UART PMU: TXRST Position */
#define CMSDK_UART_PMU_TXRST_Msk         	(0x01ul << CMSDK_UART_PMU_TXRST_Pos)        	/*!< CMSDK_UART PMU: TXRST Mask */

#define CMSDK_UART_PMU_RXRST_Pos         	0                                             	/*!< CMSDK_UART PMU: RXRST Position */
#define CMSDK_UART_PMU_RXRST_Msk         	(0x01ul << CMSDK_UART_PMU_RXRST_Pos)        	/*!< CMSDK_UART PMU: RXRST Mask */

#define CMSDK_UART_MDR_DEB_EN_Pos         	1                                             	/*!< CMSDK_UART MDR: DEB_EN Position */
#define CMSDK_UART_MDR_DEB_EN_Msk         	(0x01ul << CMSDK_UART_MDR_DEB_EN_Pos)        	/*!< CMSDK_UART MDR: DEB_EN Mask */

#define CMSDK_UART_MDR_OSM_SEL_Pos         	0                                             	/*!< CMSDK_UART MDR: OSM_SEL Position */
#define CMSDK_UART_MDR_OSM_SEL_Msk         	(0x01ul << CMSDK_UART_MDR_OSM_SEL_Pos)        	/*!< CMSDK_UART MDR: OSM_SEL Mask */
/*@}*/ /* end of group CMSDK_UART */


/*-------------------- Serial Peripheral Interface (SPI) -------------------*/

/** @addtogroup CMSDK_SPI SPI
  @{
*/
typedef struct
{
  union {
    __I    uint32_t  RBR;	    /*!< Offset: 0x000 Receiver Buffer Register (R/) */
    __O    uint32_t  THR;           /*!< Offset: 0x000 Transmitter Holding Register ( /W) */
    };
  __IO   uint32_t  IER;    	    /*!< Offset: 0x004 Interrupt Enable Register (R/W) */
  union {
    __I    uint32_t  INTSTATUS;     /*!< Offset: 0x008 Interrupt Status Register  (R/) */
    __O    uint32_t  INTCLEAR;      /*!< Offset: 0x008 Interrupt Clear Register ( /W) */
    };
  __IO   uint32_t  CTRL1;           /*!< Offset: 0x00C Control_1 Register  (R/W) */
  __IO   uint32_t  CTRL2;           /*!< Offset: 0x010 Control_2 Register  (R/W) */
  __IO   uint32_t  FCR;             /*!< Offset: 0x014 FIFO Control Register  (R/W) */
  __I    uint32_t  FSR;     	    /*!< Offset: 0x018 FIFO Status Register  (R/) */
  __I    uint32_t  DBG;      	    /*!< Offset: 0x01C Debug Signal Register  (R/) */

} CMSDK_SPI_TypeDef;


/* CMSDK_SPI  Register Definitions */
#define CMSDK_SPI_UNDERRUN_INT_EN_Pos 	4                                              	/*!< CMSDK_SPI IER: UNDERRUN_INT_EN Position */
#define CMSDK_SPI_UNDERRUN_INT_EN_Msk 	(0x01ul << CMSDK_SPI_UNDERRUN_INT_EN_Pos)       /*!< CMSDK_SPI IER: UNDERRUN_INT_EN Mask */

#define CMSDK_SPI_UNDERRUN_INT_STS_Pos 	4                                              	/*!< CMSDK_SPI ISR: UNDERRUN_INT_STS Position */
#define CMSDK_SPI_UNDERRUN_INT_STS_Msk 	(0x01ul << CMSDK_SPI_UNDERRUN_INT_STS_Pos)      /*!< CMSDK_SPI ISR: UNDERRUN_INT_STS Mask */

#define CMSDK_SPI_OVERRUN_INT_EN_Pos 	3                                              	/*!< CMSDK_SPI IER: OVERRUN_INT_EN Position */
#define CMSDK_SPI_OVERRUN_INT_EN_Msk 	(0x01ul << CMSDK_SPI_OVERRUN_INT_EN_Pos)        /*!< CMSDK_SPI IER: OVERRUN_INT_EN Mask */

#define CMSDK_SPI_OVERRUN_INT_STS_Pos 	3                                              	/*!< CMSDK_SPI ISR: OVERRUN_INT_STS Position */
#define CMSDK_SPI_OVERRUN_INT_STS_Msk 	(0x01ul << CMSDK_SPI_OVERRUN_INT_STS_Pos)       /*!< CMSDK_SPI ISR: OVERRUN_INT_STS Mask */

#define CMSDK_SPI_CMPL_INT_EN_Pos 	2                                              	/*!< CMSDK_SPI IER: CMPL_INT_EN Position */
#define CMSDK_SPI_CMPL_INT_EN_Msk 	(0x01ul << CMSDK_SPI_CMPL_INT_EN_Pos)           /*!< CMSDK_SPI IER: CMPL_INT_EN Mask */

#define CMSDK_SPI_CMPL_INT_STS_Pos 	2                                              	/*!< CMSDK_SPI ISR: CMPL_INT_STS Position */
#define CMSDK_SPI_CMPL_INT_STS_Msk 	(0x01ul << CMSDK_SPI_CMPL_INT_STS_Pos)          /*!< CMSDK_SPI ISR: CMPL_INT_STS Mask */

#define CMSDK_SPI_TXE_INT_EN_Pos 	1                                              	/*!< CMSDK_SPI IER: TXE_INT_EN Position */
#define CMSDK_SPI_TXE_INT_EN_Msk 	(0x01ul << CMSDK_SPI_TXE_INT_EN_Pos)           	/*!< CMSDK_SPI IER: TXE_INT_EN Mask */

#define CMSDK_SPI_TXE_INT_STS_Pos 	1                                              	/*!< CMSDK_SPI ISR: TXE_INT_STS Position */
#define CMSDK_SPI_TXE_INT_STS_Msk 	(0x01ul << CMSDK_SPI_TXE_INT_STS_Pos)           /*!< CMSDK_SPI ISR: TXE_INT_STS Mask */

#define CMSDK_SPI_RXNE_INT_EN_Pos 	0                                              	/*!< CMSDK_SPI IER: RXNE_INT_EN Position */
#define CMSDK_SPI_RXNE_INT_EN_Msk 	(0x01ul << CMSDK_SPI_RXNE_INT_EN_Pos)           /*!< CMSDK_SPI IER: RXNE_INT_EN Mask */

#define CMSDK_SPI_RXNE_INT_STS_Pos 	0                                              	/*!< CMSDK_SPI ISR: RXNE_INT_STS Position */
#define CMSDK_SPI_RXNE_INT_STS_Msk 	(0x01ul << CMSDK_SPI_RXNE_INT_STS_Pos)          /*!< CMSDK_SPI ISR: RXNE_INT_STS Mask */

#define CMSDK_SPI_BIDI_EN_Pos         	15                                              /*!< CMSDK_SPI CTRL1: BIDI_EN Position */
#define CMSDK_SPI_BIDI_EN_Msk         	(0x01ul << CMSDK_SPI_BIDI_EN_Pos)           	/*!< CMSDK_SPI CTRL1: BIDI_EN Mask */

#define CMSDK_SPI_BIDI_MODE_Pos         14                                              /*!< CMSDK_SPI CTRL1: BIDI_MODE Position */
#define CMSDK_SPI_BIDI_MODE_Msk         (0x01ul << CMSDK_SPI_BIDI_MODE_Pos)           	/*!< CMSDK_SPI CTRL1: BIDI_MODE Mask */

#define CMSDK_SPI_UNIDI_MODE_Pos        12                                              /*!< CMSDK_SPI CTRL1: UNIDI_MODE Position */
#define CMSDK_SPI_UNIDI_MODE_Msk        (0x03ul << CMSDK_SPI_UNIDI_MODE_Pos)           	/*!< CMSDK_SPI CTRL1: UNIDI_MODE Mask */

#define CMSDK_SPI_NSS_TOGGLE_Pos        11                                              /*!< CMSDK_SPI CTRL1: NSS_TOGGLE Position */
#define CMSDK_SPI_NSS_TOGGLE_Msk        (0x01ul << CMSDK_SPI_NSS_TOGGLE_Pos)           	/*!< CMSDK_SPI CTRL1: NSS_TOGGLE Mask */

#define CMSDK_SPI_LOOP_BACK_EN_Pos      10                                              /*!< CMSDK_SPI CTRL1: LOOP_BACK_EN Position */
#define CMSDK_SPI_LOOP_BACK_EN_Msk      (0x01ul << CMSDK_SPI_LOOP_BACK_EN_Pos)          /*!< CMSDK_SPI CTRL1: LOOP_BACK_EN Mask */

#define CMSDK_SPI_NSS_MST_SW_Pos        9                                              	/*!< CMSDK_SPI CTRL1: NSS_MST_SW Position */
#define CMSDK_SPI_NSS_MST_SW_Msk        (0x01ul << CMSDK_SPI_NSS_MST_SW_Pos)           	/*!< CMSDK_SPI CTRL1: NSS_MST_SW Mask */

#define CMSDK_SPI_NSS_MST_CTRL_Pos      8                                              	/*!< CMSDK_SPI CTRL1: NSS_MST_CTRL Position */
#define CMSDK_SPI_NSS_MST_CTRL_Msk      (0x01ul << CMSDK_SPI_NSS_MST_CTRL_Pos)          /*!< CMSDK_SPI CTRL1: NSS_MST_CTRL Mask */

#define CMSDK_SPI_LSB_SEL_Pos         	7                                              	/*!< CMSDK_SPI CTRL1: LSB_SEL Position */
#define CMSDK_SPI_LSB_SEL_Msk         	(0x01ul << CMSDK_SPI_LSB_SEL_Pos)           	/*!< CMSDK_SPI CTRL1: LSB_SEL Mask */

#define CMSDK_SPI_BAUD_RATE_Pos         4                                               /*!< CMSDK_SPI CTRL1: BAUD_RATE Position */
#define CMSDK_SPI_BAUD_RATE_Msk         (0x07ul << CMSDK_SPI_BAUD_RATE_Pos)           	/*!< CMSDK_SPI CTRL1: BAUD_RATE Mask */

#define CMSDK_SPI_CPOL_Pos         	3                                              	/*!< CMSDK_SPI CTRL1: CPOL Position */
#define CMSDK_SPI_CPOL_Msk         	(0x01ul << CMSDK_SPI_CPOL_Pos)           	/*!< CMSDK_SPI CTRL1: CPOL Mask */

#define CMSDK_SPI_CPHA_Pos         	2                                              	/*!< CMSDK_SPI CTRL1: CPHA Position */
#define CMSDK_SPI_CPHA_Msk         	(0x01ul << CMSDK_SPI_CPHA_Pos)           	/*!< CMSDK_SPI CTRL1: CPHA Mask */

#define CMSDK_SPI_MST_SLV_SEL_Pos       1                                              	/*!< CMSDK_SPI CTRL1: MST_SLV_SEL Position */
#define CMSDK_SPI_MST_SLV_SEL_Msk       (0x01ul << CMSDK_SPI_MST_SLV_SEL_Pos)           /*!< CMSDK_SPI CTRL1: MST_SLV_SEL Mask */

#define CMSDK_SPI_EN_Pos          	0                                              	/*!< CMSDK_SPI CTRL1: SPI_EN Position */
#define CMSDK_SPI_EN_Msk          	(0x01ul << CMSDK_SPI_EN_Pos)           		/*!< CMSDK_SPI CTRL1: SPI_EN Mask */

#define CMSDK_SPI_T2C_DELAY_Pos         14                                              /*!< CMSDK_SPI CTRL2: T2C_DELAY Position */
#define CMSDK_SPI_T2C_DELAY_Msk         (0x0003ul << CMSDK_SPI_T2C_DELAY_Pos)           /*!< CMSDK_SPI CTRL2: T2C_DELAY Mask */

#define CMSDK_SPI_C2T_DELAY_Pos         12                                              /*!< CMSDK_SPI CTRL2: C2T_DELAY Position */
#define CMSDK_SPI_C2T_DELAY_Msk         (0x0003ul << CMSDK_SPI_C2T_DELAY_Pos)           /*!< CMSDK_SPI CTRL2: C2T_DELAY Mask */

#define CMSDK_SPI_NSS3_EN_Pos          	11                                              /*!< CMSDK_SPI CTRL2: NSS3_EN Position */
#define CMSDK_SPI_NSS3_EN_Msk          	(0x01ul << CMSDK_SPI_NSS3_EN_Pos)           	/*!< CMSDK_SPI CTRL2: NSS3_EN Mask */

#define CMSDK_SPI_NSS2_EN_Pos          	10                                              /*!< CMSDK_SPI CTRL2: NSS2_EN Position */
#define CMSDK_SPI_NSS2_EN_Msk          	(0x01ul << CMSDK_SPI_NSS2_EN_Pos)           	/*!< CMSDK_SPI CTRL2: NSS2_EN Mask */

#define CMSDK_SPI_NSS1_EN_Pos          	9                                              	/*!< CMSDK_SPI CTRL2: NSS1_EN Position */
#define CMSDK_SPI_NSS1_EN_Msk          	(0x01ul << CMSDK_SPI_NSS1_EN_Pos)           	/*!< CMSDK_SPI CTRL2: NSS1_EN Mask */

#define CMSDK_SPI_NSS0_EN_Pos          	8                                             	/*!< CMSDK_SPI CTRL2: NSS0_EN Position */
#define CMSDK_SPI_NSS0_EN_Msk          	(0x01ul << CMSDK_SPI_NSS0_EN_Pos)           	/*!< CMSDK_SPI CTRL2: NSS0_EN Mask */

#define CMSDK_SPI_SAMP_PHASE_Pos        6                                               /*!< CMSDK_SPI CTRL2: SAMP_PHASE Position */
#define CMSDK_SPI_SAMP_PHASE_Msk        (0x0003ul << CMSDK_SPI_SAMP_PHASE_Pos)          /*!< CMSDK_SPI CTRL2: SAMP_PHASE Mask */

#define CMSDK_SPI_TX_DMA_EN_Pos         5                                             	/*!< CMSDK_SPI CTRL2: TX_DMA_EN Position */
#define CMSDK_SPI_TX_DMA_EN_Msk         (0x01ul << CMSDK_SPI_TX_DMA_EN_Pos)           	/*!< CMSDK_SPI CTRL2: TX_DMA_EN Mask */

#define CMSDK_SPI_RX_DMA_EN_Pos         4                                             	/*!< CMSDK_SPI CTRL2: RX_DMA_EN Position */
#define CMSDK_SPI_RX_DMA_EN_Msk         (0x01ul << CMSDK_SPI_RX_DMA_EN_Pos)           	/*!< CMSDK_SPI CTRL2: RX_DMA_EN Mask */

#define CMSDK_SPI_CHAR_LEN_Pos         	0                                              	/*!< CMSDK_SPI CTRL2: CHAR_LEN Position */
#define CMSDK_SPI_CHAR_LEN_Msk         	(0x000Ful << CMSDK_SPI_CHAR_LEN_Pos)           	/*!< CMSDK_SPI CTRL2: CHAR_LEN Mask */

#define CMSDK_SPI_RX_FIFO_LEN_Pos 	16                                              /*!< CMSDK_SPI FSR: RX_FIFO_LEN Position */
#define CMSDK_SPI_RX_FIFO_LEN_Msk 	(0x001Ful << CMSDK_SPI_RX_FIFO_LEN_Pos)         /*!< CMSDK_SPI FSR: RX_FIFO_LEN Mask */

#define CMSDK_SPI_TX_FIFO_LEN_Pos 	8                                              	/*!< CMSDK_SPI FSR: TX_FIFO_LEN Position */
#define CMSDK_SPI_TX_FIFO_LEN_Msk 	(0x001Ful << CMSDK_SPI_TX_FIFO_LEN_Pos)         /*!< CMSDK_SPI FSR: TX_FIFO_LEN Mask */

#define CMSDK_SPI_BUSY_Pos          	4                                              	/*!< CMSDK_SPI FSR: SPI_BUSY Position */
#define CMSDK_SPI_BUSY_Msk          	(0x01ul << CMSDK_SPI_BUSY_Pos)           	/*!< CMSDK_SPI FSR: SPI_BUSY Mask */

#define CMSDK_SPI_RX_FIFO_FULL_Pos      3                                              	/*!< CMSDK_SPI FSR: RX_FIFO_FULL Position */
#define CMSDK_SPI_RX_FIFO_FULL_Msk      (0x01ul << CMSDK_SPI_RX_FIFO_FULL_Pos)          /*!< CMSDK_SPI FSR: RX_FIFO_FULL Mask */

#define CMSDK_SPI_TX_FIFO_EMPTY_Pos     0                                              	/*!< CMSDK_SPI FSR: TX_FIFO_EMPTY Position */
#define CMSDK_SPI_TX_FIFO_EMPTY_Msk     (0x01ul << CMSDK_SPI_TX_FIFO_EMPTY_Pos)         /*!< CMSDK_SPI FSR: TX_FIFO_EMPTY Mask */

#define CMSDK_SPI_TX_FIFO_TH_Pos 	9                                              	/*!< CMSDK_SPI FCR: TX_FIFO_TH Position */
#define CMSDK_SPI_TX_FIFO_TH_Msk 	(0x001Ful << CMSDK_SPI_TX_FIFO_TH_Pos)          /*!< CMSDK_SPI FCR: TX_FIFO_TH Mask */

#define CMSDK_SPI_TX_FIFO_CLR_Pos       8                                              	/*!< CMSDK_SPI FCR: TX_FIFO_CLR Position */
#define CMSDK_SPI_TX_FIFO_CLR_Msk       (0x01ul << CMSDK_SPI_TX_FIFO_CLR_Pos)           /*!< CMSDK_SPI FCR: TX_FIFO_CLR Mask */

#define CMSDK_SPI_RX_FIFO_TH_Pos 	2                                              	/*!< CMSDK_SPI FCR: RX_FIFO_TH Position */
#define CMSDK_SPI_RX_FIFO_TH_Msk 	(0x001Ful << CMSDK_SPI_RX_FIFO_TH_Pos)          /*!< CMSDK_SPI FCR: RX_FIFO_TH Mask */

#define CMSDK_SPI_RX_FIFO_CLR_Pos       1                                              	/*!< CMSDK_SPI FCR: RX_FIFO_CLR Position */
#define CMSDK_SPI_RX_FIFO_CLR_Msk       (0x01ul << CMSDK_SPI_RX_FIFO_CLR_Pos)           /*!< CMSDK_SPI FCR: RX_FIFO_CLR Mask */

#define CMSDK_SPI_FIFO_EN_Pos 		0                                              	/*!< CMSDK_SPI FCR: FIFO_EN Position */
#define CMSDK_SPI_FIFO_EN_Msk 		(0x01ul << CMSDK_SPI_FIFO_EN_Pos)           	/*!< CMSDK_SPI FCR: FIFO_EN Mask */
/*@}*/ /* end of group CMSDK_SPI */


/*-------------------- Inter Integrated Circuit Bus(I2C) -------------------*/

/** @addtogroup CMSDK_I2C I2C
  @{
*/
typedef struct
{
  __IO   uint32_t  I2C_DR;    	    /*!< Offset: 0x000 I2C Data Register (R/W) */
  __IO   uint32_t  I2C_OAR;    	    /*!< Offset: 0x004 I2C Own Address Register (R/W) */
  __IO   uint32_t  I2C_CR1;         /*!< Offset: 0x008 I2C Control Register 1  (R/W) */
  __IO   uint32_t  I2C_CR2;         /*!< Offset: 0x00C I2C Control Register 2  (R/W) */
  union {
    __I    uint32_t  I2C_STS;       /*!< Offset: 0x010 I2C Status Register SR (R/) */
    __O    uint32_t  I2C_STS_CLR;   /*!< Offset: 0x010 I2C Status Clear Register ( /W) */
    };
  __I    uint32_t  I2C_DBG;   	    /*!< Offset: 0x014 I2C Debug data Register  (R/) */

} CMSDK_I2C_TypeDef;


/* CMSDK_I2C  Register Definitions */
#define CMSDK_I2C_CR1_SWRST_Pos 	15                                              /*!< CMSDK_I2C CR1: SWRST Position */
#define CMSDK_I2C_CR1_SWRST_Msk 	(0x01ul << CMSDK_I2C_CR1_SWRST_Pos)           	/*!< CMSDK_I2C CR1: SWRST Mask */

#define CMSDK_I2C_CR1_DBYPASS_Pos 	7                                              	/*!< CMSDK_I2C CR1: DBYPASS Position */
#define CMSDK_I2C_CR1_DBYPASS_Msk 	(0x01ul << CMSDK_I2C_CR1_DBYPASS_Pos)           /*!< CMSDK_I2C CR1: DBYPASS Mask */

#define CMSDK_I2C_CR1_MULTIMASTER_Pos 	6                                              	/*!< CMSDK_I2C CR1: MULTIMASTER Position */
#define CMSDK_I2C_CR1_MULTIMASTER_Msk 	(0x01ul << CMSDK_I2C_CR1_MULTIMASTER_Pos)       /*!< CMSDK_I2C CR1: MULTIMASTER Mask */

#define CMSDK_I2C_CR1_ACK_Pos 		5                                              	/*!< CMSDK_I2C CR1: ACK Position */
#define CMSDK_I2C_CR1_ACK_Msk 		(0x01ul << CMSDK_I2C_CR1_ACK_Pos)           	/*!< CMSDK_I2C CR1: ACK Mask */

#define CMSDK_I2C_CR1_STOP_Pos 		4                                              	/*!< CMSDK_I2C CR1: STOP Position */
#define CMSDK_I2C_CR1_STOP_Msk 		(0x01ul << CMSDK_I2C_CR1_STOP_Pos)           	/*!< CMSDK_I2C CR1: STOP Mask */

#define CMSDK_I2C_CR1_START_Pos 	3                                              	/*!< CMSDK_I2C CR1: START Position */
#define CMSDK_I2C_CR1_START_Msk 	(0x01ul << CMSDK_I2C_CR1_START_Pos)           	/*!< CMSDK_I2C CR1: START Mask */

#define CMSDK_I2C_CR1_NOSTRETCH_Pos 	2                                              	/*!< CMSDK_I2C CR1: NOSTRETCH Position */
#define CMSDK_I2C_CR1_NOSTRETCH_Msk 	(0x01ul << CMSDK_I2C_CR1_NOSTRETCH_Pos)         /*!< CMSDK_I2C CR1: NOSTRETCH Mask */

#define CMSDK_I2C_CR1_ENGC_Pos 		1                                              	/*!< CMSDK_I2C CR1: ENGC Position */
#define CMSDK_I2C_CR1_ENGC_Msk 		(0x01ul << CMSDK_I2C_CR1_ENGC_Pos)           	/*!< CMSDK_I2C CR1: ENGC Mask */

#define CMSDK_I2C_CR1_PE_Pos 		0                                              	/*!< CMSDK_I2C CR1: PE Position */
#define CMSDK_I2C_CR1_PE_Msk 		(0x01ul << CMSDK_I2C_CR1_PE_Pos)           	/*!< CMSDK_I2C CR1: PE Mask */

#define CMSDK_I2C_CR2_DMALAST_Pos 	10                                              /*!< CMSDK_I2C CR2: DMALAST Position */
#define CMSDK_I2C_CR2_DMALAST_Msk 	(0x01ul << CMSDK_I2C_CR2_DMALAST_Pos)           /*!< CMSDK_I2C CR2: DMALAST Mask */

#define CMSDK_I2C_CR2_DMAEN_Pos 	9                                              	/*!< CMSDK_I2C CR2: DMAEN Position */
#define CMSDK_I2C_CR2_DMAEN_Msk 	(0x01ul << CMSDK_I2C_CR2_DMAEN_Pos)             /*!< CMSDK_I2C CR2: DMAEN Mask */

#define CMSDK_I2C_CR2_BUF_INTEN_Pos 	8                                              	/*!< CMSDK_I2C CR2: BUF_INTEN Position */
#define CMSDK_I2C_CR2_BUF_INTEN_Msk 	(0x01ul << CMSDK_I2C_CR2_BUF_INTEN_Pos)         /*!< CMSDK_I2C CR2: BUF_INTEN Mask */

#define CMSDK_I2C_CR2_EVT_INTEN_Pos 	7                                              	/*!< CMSDK_I2C CR2: EVT_INTEN Position */
#define CMSDK_I2C_CR2_EVT_INTEN_Msk 	(0x01ul << CMSDK_I2C_CR2_EVT_INTEN_Pos)         /*!< CMSDK_I2C CR2: EVT_INTEN Mask */

#define CMSDK_I2C_CR2_ERR_INTEN_Pos 	6                                              	/*!< CMSDK_I2C CR2: ERR_INTEN Position */
#define CMSDK_I2C_CR2_ERR_INTEN_Msk 	(0x01ul << CMSDK_I2C_CR2_ERR_INTEN_Pos)         /*!< CMSDK_I2C CR2: ERR_INTEN Mask */

#define CMSDK_I2C_CR2_FREQDIV_Pos 	0                                              	/*!< CMSDK_I2C CR2: FREQDIV Position */
#define CMSDK_I2C_CR2_FREQDIV_Msk 	(0x003Ful << CMSDK_I2C_CR2_FREQDIV_Pos)         /*!< CMSDK_I2C CR2: FREQDIV Mask */

#define CMSDK_I2C_SR_GENCALL_Pos 	15                                              /*!< CMSDK_I2C SR: GENCALL Position */
#define CMSDK_I2C_SR_GENCALL_Msk 	(0x01ul << CMSDK_I2C_SR_GENCALL_Pos)           	/*!< CMSDK_I2C SR: GENCALL Mask */

#define CMSDK_I2C_SR_TX_RX_Pos 		14                                              /*!< CMSDK_I2C SR: TX_RX Position */
#define CMSDK_I2C_SR_TX_RX_Msk 		(0x01ul << CMSDK_I2C_SR_TX_RX_Pos)           	/*!< CMSDK_I2C SR: TX_RX Mask */

#define CMSDK_I2C_SR_BUSY_Pos 		13                                              /*!< CMSDK_I2C SR: BUSY Position */
#define CMSDK_I2C_SR_BUSY_Msk 		(0x01ul << CMSDK_I2C_SR_BUSY_Pos)           	/*!< CMSDK_I2C SR: BUSY Mask */

#define CMSDK_I2C_SR_MSL_Pos 		12                                              /*!< CMSDK_I2C SR: MSL Position */
#define CMSDK_I2C_SR_MSL_Msk 		(0x01ul << CMSDK_I2C_SR_MSL_Pos)           	/*!< CMSDK_I2C SR: MSL Mask */

#define CMSDK_I2C_SR_OVR_Pos 		11                                              /*!< CMSDK_I2C SR: OVR Position */
#define CMSDK_I2C_SR_OVR_Msk 		(0x01ul << CMSDK_I2C_SR_OVR_Pos)           	/*!< CMSDK_I2C SR: OVR Mask */

#define CMSDK_I2C_SR_ACK_FAIL_Pos 	10                                              /*!< CMSDK_I2C SR: ACK_FAIL Position */
#define CMSDK_I2C_SR_ACK_FAIL_Msk 	(0x01ul << CMSDK_I2C_SR_ACK_FAIL_Pos)           /*!< CMSDK_I2C SR: ACK_FAIL Mask */

#define CMSDK_I2C_SR_ARB_LOST_Pos 	9                                              	/*!< CMSDK_I2C SR: ARB_LOST Position */
#define CMSDK_I2C_SR_ARB_LOST_Msk 	(0x01ul << CMSDK_I2C_SR_ARB_LOST_Pos)           /*!< CMSDK_I2C SR: ARB_LOST Mask */

#define CMSDK_I2C_SR_BUS_ERR_Pos 	8                                              	/*!< CMSDK_I2C SR: BUS_ERR Position */
#define CMSDK_I2C_SR_BUS_ERR_Msk 	(0x01ul << CMSDK_I2C_SR_BUS_ERR_Pos)           	/*!< CMSDK_I2C SR: BUS_ERR Mask */

#define CMSDK_I2C_SR_TXE_Pos 		6                                              	/*!< CMSDK_I2C SR: TXE Position */
#define CMSDK_I2C_SR_TXE_Msk 		(0x01ul << CMSDK_I2C_SR_TXE_Pos)           	/*!< CMSDK_I2C SR: TXE Mask */

#define CMSDK_I2C_SR_RXNE_Pos 		5                                              	/*!< CMSDK_I2C SR: RXNE Position */
#define CMSDK_I2C_SR_RXNE_Msk 		(0x01ul << CMSDK_I2C_SR_RXNE_Pos)           	/*!< CMSDK_I2C SR: RXNE Mask */

#define CMSDK_I2C_SR_STOPF_Pos 		4                                              	/*!< CMSDK_I2C SR: STOPF Position */
#define CMSDK_I2C_SR_STOPF_Msk 		(0x01ul << CMSDK_I2C_SR_STOPF_Pos)           	/*!< CMSDK_I2C SR: STOPF Mask */

#define CMSDK_I2C_SR_ADD10_Pos 		3                                              	/*!< CMSDK_I2C SR: ADD10 Position */
#define CMSDK_I2C_SR_ADD10_Msk 		(0x01ul << CMSDK_I2C_SR_ADD10_Pos)           	/*!< CMSDK_I2C SR: ADD10 Mask */

#define CMSDK_I2C_SR_BTF_Pos 		2                                              	/*!< CMSDK_I2C SR: BTF Position */
#define CMSDK_I2C_SR_BTF_Msk 		(0x01ul << CMSDK_I2C_SR_BTF_Pos)           	/*!< CMSDK_I2C SR: BTF Mask */

#define CMSDK_I2C_SR_ADDR_Pos 		1                                              	/*!< CMSDK_I2C SR: ADDR Position */
#define CMSDK_I2C_SR_ADDR_Msk 		(0x01ul << CMSDK_I2C_SR_ADDR_Pos)           	/*!< CMSDK_I2C SR: ADDR Mask */

#define CMSDK_I2C_SR_STARTBIT_Pos 	0                                              	/*!< CMSDK_I2C SR: STARTBIT Position */
#define CMSDK_I2C_SR_STARTBIT_Msk 	(0x01ul << CMSDK_I2C_SR_STARTBIT_Pos)           /*!< CMSDK_I2C SR: STARTBIT Mask */
/*@}*/ /* end of group CMSDK_I2C */


/*------------------- Watchdog ----------------------------------------------*/
/** @addtogroup CMSDK_WATCHDOG Watchdog
  @{
*/
typedef struct
{

  __IO    uint32_t  LOAD;          // <h> Watchdog Load Register </h>
  __I     uint32_t  VALUE;         // <h> Watchdog Value Register </h>
  __IO    uint32_t  CTRL;          // <h> Watchdog Control Register
                                   //   <o.1>    RESEN: Reset enable
                                   //   <o.0>    INTEN: Interrupt enable
                                   // </h>
  __O     uint32_t  INTCLR;        // <h> Watchdog Clear Interrupt Register </h>
  __I     uint32_t  RAWINTSTAT;    // <h> Watchdog Raw Interrupt Status Register </h>
  __I     uint32_t  MASKINTSTAT;   // <h> Watchdog Interrupt Status Register </h>
        uint32_t  RESERVED0[762];
  __IO    uint32_t  LOCK;          // <h> Watchdog Lock Register </h>
        uint32_t  RESERVED1[191];
  __IO    uint32_t  ITCR;          // <h> Watchdog Integration Test Control Register </h>
  __O     uint32_t  ITOP;          // <h> Watchdog Integration Test Output Set Register </h>

}CMSDK_WDT_TypeDef;

/* CMSDK_WATCHDOG  Register Definitions */
#define CMSDK_Watchdog_LOAD_Pos               0                                              /*!< CMSDK_Watchdog LOAD: LOAD Position */
#define CMSDK_Watchdog_LOAD_Msk               (0xFFFFFFFFul << CMSDK_Watchdog_LOAD_Pos)      /*!< CMSDK_Watchdog LOAD: LOAD Mask */

#define CMSDK_Watchdog_VALUE_Pos              0                                              /*!< CMSDK_Watchdog VALUE: VALUE Position */
#define CMSDK_Watchdog_VALUE_Msk              (0xFFFFFFFFul << CMSDK_Watchdog_VALUE_Pos)     /*!< CMSDK_Watchdog VALUE: VALUE Mask */

#define CMSDK_Watchdog_CTRL_RESEN_Pos         1                                              /*!< CMSDK_Watchdog CTRL_RESEN: Enable Reset Output Position */
#define CMSDK_Watchdog_CTRL_RESEN_Msk         (0x1ul << CMSDK_Watchdog_CTRL_RESEN_Pos)       /*!< CMSDK_Watchdog CTRL_RESEN: Enable Reset Output Mask */

#define CMSDK_Watchdog_CTRL_INTEN_Pos         0                                              /*!< CMSDK_Watchdog CTRL_INTEN: Int Enable Position */
#define CMSDK_Watchdog_CTRL_INTEN_Msk         (0x1ul << CMSDK_Watchdog_CTRL_INTEN_Pos)       /*!< CMSDK_Watchdog CTRL_INTEN: Int Enable Mask */

#define CMSDK_Watchdog_INTCLR_Pos             0                                              /*!< CMSDK_Watchdog INTCLR: Int Clear Position */
#define CMSDK_Watchdog_INTCLR_Msk             (0x1ul << CMSDK_Watchdog_INTCLR_Pos)           /*!< CMSDK_Watchdog INTCLR: Int Clear Mask */

#define CMSDK_Watchdog_RAWINTSTAT_Pos         0                                              /*!< CMSDK_Watchdog RAWINTSTAT: Raw Int Status Position */
#define CMSDK_Watchdog_RAWINTSTAT_Msk         (0x1ul << CMSDK_Watchdog_RAWINTSTAT_Pos)       /*!< CMSDK_Watchdog RAWINTSTAT: Raw Int Status Mask */

#define CMSDK_Watchdog_MASKINTSTAT_Pos        0                                              /*!< CMSDK_Watchdog MASKINTSTAT: Mask Int Status Position */
#define CMSDK_Watchdog_MASKINTSTAT_Msk        (0x1ul << CMSDK_Watchdog_MASKINTSTAT_Pos)      /*!< CMSDK_Watchdog MASKINTSTAT: Mask Int Status Mask */

#define CMSDK_Watchdog_LOCK_Pos               0                                              /*!< CMSDK_Watchdog LOCK: LOCK Position */
#define CMSDK_Watchdog_LOCK_Msk               (0x1ul << CMSDK_Watchdog_LOCK_Pos)             /*!< CMSDK_Watchdog LOCK: LOCK Mask */

#define CMSDK_Watchdog_INTEGTESTEN_Pos        0                                              /*!< CMSDK_Watchdog INTEGTESTEN: Integration Test Enable Position */
#define CMSDK_Watchdog_INTEGTESTEN_Msk        (0x1ul << CMSDK_Watchdog_INTEGTESTEN_Pos)      /*!< CMSDK_Watchdog INTEGTESTEN: Integration Test Enable Mask */

#define CMSDK_Watchdog_INTEGTESTOUTSET_Pos    1                                              /*!< CMSDK_Watchdog INTEGTESTOUTSET: Integration Test Output Set Position */
#define CMSDK_Watchdog_INTEGTESTOUTSET_Msk    (0x1ul << CMSDK_Watchdog_INTEGTESTOUTSET_Pos)  /*!< CMSDK_Watchdog INTEGTESTOUTSET: Integration Test Output Set Mask */
/*@}*/ /* end of group CMSDK_WATCHDOG */


/*------------------- Pulse Width Modulator (PWM) -------------------*/

/** @addtogroup CMSDK_PWM PWM
  @{
*/
typedef struct
{
  __IO   uint32_t  TC;    	    /*!< Offset: 0x000 PWM Timer Counter Register (R/W) */
  __IO   uint32_t  TCR;    	    /*!< Offset: 0x004 PWM Timer Control Register (R/W) */
  union {
    __I    uint32_t  INTSTATUS;     /*!< Offset: 0x008 Interrupt Status Register  (R/) */
    __O    uint32_t  INTCLEAR;      /*!< Offset: 0x008 Interrupt Clear Register ( /W) */
    };
  __IO   uint32_t  PR;              /*!< Offset: 0x00C PWM Prescale Register  (R/W) */
  __IO   uint32_t  PC;              /*!< Offset: 0x010 PWM Prescale Counter Register  (R/W) */
  __IO   uint32_t  MCR;             /*!< Offset: 0x014 PWM Match Control Register  (R/W) */
  __IO   uint32_t  MR0;             /*!< Offset: 0x018 PWM Match Register 0 (R/W) */
  __IO   uint32_t  MR1;             /*!< Offset: 0x01C PWM Match Register 1 (R/W) */
  __IO   uint32_t  MR2;             /*!< Offset: 0x020 PWM Match Register 2 (R/W) */
  __IO   uint32_t  MR3;             /*!< Offset: 0x024 PWM Match Register 3 (R/W) */
  __IO   uint32_t  MR4;             /*!< Offset: 0x028 PWM Match Register 4 (R/W) */
  __IO   uint32_t  MR5;             /*!< Offset: 0x02C PWM Match Register 5 (R/W) */
  __IO   uint32_t  MR6;             /*!< Offset: 0x030 PWM Match Register 6 (R/W) */
  __IO   uint32_t  PCR;     	    /*!< Offset: 0x034 PWM Control Register  (R/W) */
  __IO   uint32_t  LER;      	    /*!< Offset: 0x038 PWM Load Enable Register  (R/W) */
  __I    uint32_t  DBG;     	    /*!< Offset: 0x03C PWM Debug Register  (R/) */

} CMSDK_PWM_TypeDef;


/* CMSDK_PWM  Register Definitions */
#define CMSDK_PWM_TIMER_CNT_EN_Pos 	1                                             /*!< CMSDK_PWM TCR: CNT_EN Position */
#define CMSDK_PWM_TIMER_CNT_EN_Msk 	(0x01ul << CMSDK_PWM_TIMER_CNT_EN_Pos)        /*!< CMSDK_PWM TCR: CNT_EN Mask */

#define CMSDK_PWM_TIMER_CNT_RET_Pos 	0                                             /*!< CMSDK_PWM TCR: CNT_RET Position */
#define CMSDK_PWM_TIMER_CNT_RET_Msk 	(0x01ul << CMSDK_PWM_TIMER_CNT_RET_Pos)       /*!< CMSDK_PWM TCR: CNT_RET Mask */

#define CMSDK_PWM_MR6_INT_STS_Pos 	6                                             /*!< CMSDK_PWM IR: PWMMR6_INT Position */
#define CMSDK_PWM_MR6_INT_STS_Msk 	(0x01ul << CMSDK_PWM_MR6_INT_STS_Pos)         /*!< CMSDK_PWM IR: PWMMR6_INT Mask */

#define CMSDK_PWM_MR5_INT_STS_Pos 	5                                             /*!< CMSDK_PWM IR: PWMMR5_INT Position */
#define CMSDK_PWM_MR5_INT_STS_Msk 	(0x01ul << CMSDK_PWM_MR5_INT_STS_Pos)         /*!< CMSDK_PWM IR: PWMMR5_INT Mask */

#define CMSDK_PWM_MR4_INT_STS_Pos 	4                                             /*!< CMSDK_PWM IR: PWMMR4_INT Position */
#define CMSDK_PWM_MR4_INT_STS_Msk 	(0x01ul << CMSDK_PWM_MR4_INT_STS_Pos)         /*!< CMSDK_PWM IR: PWMMR4_INT Mask */

#define CMSDK_PWM_MR3_INT_STS_Pos 	3                                             /*!< CMSDK_PWM IR: PWMMR3_INT Position */
#define CMSDK_PWM_MR3_INT_STS_Msk 	(0x01ul << CMSDK_PWM_MR3_INT_STS_Pos)         /*!< CMSDK_PWM IR: PWMMR3_INT Mask */

#define CMSDK_PWM_MR2_INT_STS_Pos 	2                                             /*!< CMSDK_PWM IR: PWMMR2_INT Position */
#define CMSDK_PWM_MR2_INT_STS_Msk 	(0x01ul << CMSDK_PWM_MR2_INT_STS_Pos)         /*!< CMSDK_PWM IR: PWMMR2_INT Mask */

#define CMSDK_PWM_MR1_INT_STS_Pos 	1                                             /*!< CMSDK_PWM IR: PWMMR1_INT Position */
#define CMSDK_PWM_MR1_INT_STS_Msk 	(0x01ul << CMSDK_PWM_MR1_INT_STS_Pos)         /*!< CMSDK_PWM IR: PWMMR1_INT Mask */

#define CMSDK_PWM_MR0_INT_STS_Pos 	0                                             /*!< CMSDK_PWM IR: PWMMR0_INT Position */
#define CMSDK_PWM_MR0_INT_STS_Msk 	(0x01ul << CMSDK_PWM_MR0_INT_STS_Pos)         /*!< CMSDK_PWM IR: PWMMR0_INT Mask */

#define CMSDK_PWM_MATCH_MR6_STP_Pos 	20                                            /*!< CMSDK_PWM MCR: PWMMR6_STP Position */
#define CMSDK_PWM_MATCH_MR6_STP_Msk 	(0x01ul << CMSDK_PWM_MATCH_MR6_STP_Pos)       /*!< CMSDK_PWM MCR: PWMMR6_STP Mask */

#define CMSDK_PWM_MATCH_MR6_RET_Pos 	19                                            /*!< CMSDK_PWM MCR: PWMMR6_RET Position */
#define CMSDK_PWM_MATCH_MR6_RET_Msk 	(0x01ul << CMSDK_PWM_MATCH_MR6_RET_Pos)       /*!< CMSDK_PWM MCR: PWMMR6_RET Mask */

#define CMSDK_PWM_MATCH_MR6_INT_Pos 	18                                            /*!< CMSDK_PWM MCR: PWMMR6_INT Position */
#define CMSDK_PWM_MATCH_MR6_INT_Msk 	(0x01ul << CMSDK_PWM_MATCH_MR6_INT_Pos)       /*!< CMSDK_PWM MCR: PWMMR6_INT Mask */

#define CMSDK_PWM_MATCH_MR5_STP_Pos 	17                                            /*!< CMSDK_PWM MCR: PWMMR5_STP Position */
#define CMSDK_PWM_MATCH_MR5_STP_Msk 	(0x01ul << CMSDK_PWM_MATCH_MR5_STP_Pos)       /*!< CMSDK_PWM MCR: PWMMR5_STP Mask */

#define CMSDK_PWM_MATCH_MR5_RET_Pos 	16                                            /*!< CMSDK_PWM MCR: PWMMR5_RET Position */
#define CMSDK_PWM_MATCH_MR5_RET_Msk 	(0x01ul << CMSDK_PWM_MATCH_MR5_RET_Pos)       /*!< CMSDK_PWM MCR: PWMMR5_RET Mask */

#define CMSDK_PWM_MATCH_MR5_INT_Pos 	15                                            /*!< CMSDK_PWM MCR: PWMMR5_INT Position */
#define CMSDK_PWM_MATCH_MR5_INT_Msk 	(0x01ul << CMSDK_PWM_MATCH_MR5_INT_Pos)       /*!< CMSDK_PWM MCR: PWMMR5_INT Mask */

#define CMSDK_PWM_MATCH_MR4_STP_Pos 	14                                            /*!< CMSDK_PWM MCR: PWMMR4_STP Position */
#define CMSDK_PWM_MATCH_MR4_STP_Msk 	(0x01ul << CMSDK_PWM_MATCH_MR4_STP_Pos)       /*!< CMSDK_PWM MCR: PWMMR4_STP Mask */

#define CMSDK_PWM_MATCH_MR4_RET_Pos 	13                                            /*!< CMSDK_PWM MCR: PWMMR4_RET Position */
#define CMSDK_PWM_MATCH_MR4_RET_Msk 	(0x01ul << CMSDK_PWM_MATCH_MR4_RET_Pos)       /*!< CMSDK_PWM MCR: PWMMR4_RET Mask */

#define CMSDK_PWM_MATCH_MR4_INT_Pos 	12                                            /*!< CMSDK_PWM MCR: PWMMR4_INT Position */
#define CMSDK_PWM_MATCH_MR4_INT_Msk 	(0x01ul << CMSDK_PWM_MATCH_MR4_INT_Pos)       /*!< CMSDK_PWM MCR: PWMMR4_INT Mask */

#define CMSDK_PWM_MATCH_MR3_STP_Pos 	11                                            /*!< CMSDK_PWM MCR: PWMMR3_STP Position */
#define CMSDK_PWM_MATCH_MR3_STP_Msk 	(0x01ul << CMSDK_PWM_MATCH_MR3_STP_Pos)       /*!< CMSDK_PWM MCR: PWMMR3_STP Mask */

#define CMSDK_PWM_MATCH_MR3_RET_Pos 	10                                            /*!< CMSDK_PWM MCR: PWMMR3_RET Position */
#define CMSDK_PWM_MATCH_MR3_RET_Msk 	(0x01ul << CMSDK_PWM_MATCH_MR3_RET_Pos)       /*!< CMSDK_PWM MCR: PWMMR3_RET Mask */

#define CMSDK_PWM_MATCH_MR3_INT_Pos 	9                                             /*!< CMSDK_PWM MCR: PWMMR3_INT Position */
#define CMSDK_PWM_MATCH_MR3_INT_Msk 	(0x01ul << CMSDK_PWM_MATCH_MR3_INT_Pos)       /*!< CMSDK_PWM MCR: PWMMR3_INT Mask */

#define CMSDK_PWM_MATCH_MR2_STP_Pos 	8                                             /*!< CMSDK_PWM MCR: PWMMR2_STP Position */
#define CMSDK_PWM_MATCH_MR2_STP_Msk 	(0x01ul << CMSDK_PWM_MATCH_MR2_STP_Pos)       /*!< CMSDK_PWM MCR: PWMMR2_STP Mask */

#define CMSDK_PWM_MATCH_MR2_RET_Pos 	7                                             /*!< CMSDK_PWM MCR: PWMMR2_RET Position */
#define CMSDK_PWM_MATCH_MR2_RET_Msk 	(0x01ul << CMSDK_PWM_MATCH_MR2_RET_Pos)       /*!< CMSDK_PWM MCR: PWMMR2_RET Mask */

#define CMSDK_PWM_MATCH_MR2_INT_Pos 	6                                             /*!< CMSDK_PWM MCR: PWMMR2_INT Position */
#define CMSDK_PWM_MATCH_MR2_INT_Msk 	(0x01ul << CMSDK_PWM_MATCH_MR2_INT_Pos)       /*!< CMSDK_PWM MCR: PWMMR2_INT Mask */

#define CMSDK_PWM_MATCH_MR1_STP_Pos 	5                                             /*!< CMSDK_PWM MCR: PWMMR1_STP Position */
#define CMSDK_PWM_MATCH_MR1_STP_Msk 	(0x01ul << CMSDK_PWM_MATCH_MR1_STP_Pos)       /*!< CMSDK_PWM MCR: PWMMR1_STP Mask */

#define CMSDK_PWM_MATCH_MR1_RET_Pos 	4                                             /*!< CMSDK_PWM MCR: PWMMR1_RET Position */
#define CMSDK_PWM_MATCH_MR1_RET_Msk 	(0x01ul << CMSDK_PWM_MATCH_MR1_RET_Pos)       /*!< CMSDK_PWM MCR: PWMMR1_RET Mask */

#define CMSDK_PWM_MATCH_MR1_INT_Pos 	3                                             /*!< CMSDK_PWM MCR: PWMMR1_INT Position */
#define CMSDK_PWM_MATCH_MR1_INT_Msk 	(0x01ul << CMSDK_PWM_MATCH_MR1_INT_Pos)       /*!< CMSDK_PWM MCR: PWMMR1_INT Mask */

#define CMSDK_PWM_MATCH_MR0_STP_Pos 	2                                             /*!< CMSDK_PWM MCR: PWMMR0_STP Position */
#define CMSDK_PWM_MATCH_MR0_STP_Msk 	(0x01ul << CMSDK_PWM_MATCH_MR0_STP_Pos)       /*!< CMSDK_PWM MCR: PWMMR0_STP Mask */

#define CMSDK_PWM_MATCH_MR0_RET_Pos 	1                                             /*!< CMSDK_PWM MCR: PWMMR0_RET Position */
#define CMSDK_PWM_MATCH_MR0_RET_Msk 	(0x01ul << CMSDK_PWM_MATCH_MR0_RET_Pos)       /*!< CMSDK_PWM MCR: PWMMR0_RET Mask */

#define CMSDK_PWM_MATCH_MR0_INT_Pos 	0                                             /*!< CMSDK_PWM MCR: PWMMR0_INT Position */
#define CMSDK_PWM_MATCH_MR0_INT_Msk 	(0x01ul << CMSDK_PWM_MATCH_MR0_INT_Pos)       /*!< CMSDK_PWM MCR: PWMMR0_INT Mask */

#define CMSDK_PWM_CTRL_PWM6_EN_Pos 	10                                            /*!< CMSDK_PWM PCR: PWMEN6 Position */
#define CMSDK_PWM_CTRL_PWM6_EN_Msk 	(0x01ul << CMSDK_PWM_CTRL_PWM6_EN_Pos)        /*!< CMSDK_PWM PCR: PWMEN6 Mask */

#define CMSDK_PWM_CTRL_PWM5_EN_Pos 	9                                             /*!< CMSDK_PWM PCR: PWMEN5 Position */
#define CMSDK_PWM_CTRL_PWM5_EN_Msk 	(0x01ul << CMSDK_PWM_CTRL_PWM5_EN_Pos)        /*!< CMSDK_PWM PCR: PWMEN5 Mask */

#define CMSDK_PWM_CTRL_PWM4_EN_Pos 	8                                             /*!< CMSDK_PWM PCR: PWMEN4 Position */
#define CMSDK_PWM_CTRL_PWM4_EN_Msk 	(0x01ul << CMSDK_PWM_CTRL_PWM4_EN_Pos)        /*!< CMSDK_PWM PCR: PWMEN4 Mask */

#define CMSDK_PWM_CTRL_PWM3_EN_Pos 	7                                             /*!< CMSDK_PWM PCR: PWMEN3 Position */
#define CMSDK_PWM_CTRL_PWM3_EN_Msk 	(0x01ul << CMSDK_PWM_CTRL_PWM3_EN_Pos)        /*!< CMSDK_PWM PCR: PWMEN3 Mask */

#define CMSDK_PWM_CTRL_PWM2_EN_Pos 	6                                             /*!< CMSDK_PWM PCR: PWMEN2 Position */
#define CMSDK_PWM_CTRL_PWM2_EN_Msk 	(0x01ul << CMSDK_PWM_CTRL_PWM2_EN_Pos)        /*!< CMSDK_PWM PCR: PWMEN2 Mask */

#define CMSDK_PWM_CTRL_PWM1_EN_Pos 	5                                             /*!< CMSDK_PWM PCR: PWMEN1 Position */
#define CMSDK_PWM_CTRL_PWM1_EN_Msk 	(0x01ul << CMSDK_PWM_CTRL_PWM1_EN_Pos)        /*!< CMSDK_PWM PCR: PWMEN1 Mask */

#define CMSDK_PWM_CTRL_PWM6_SEL_Pos 	4                                             /*!< CMSDK_PWM PCR: PWMSEL6 Position */
#define CMSDK_PWM_CTRL_PWM6_SEL_Msk 	(0x01ul << CMSDK_PWM_CTRL_PWM6_SEL_Pos)       /*!< CMSDK_PWM PCR: PWMSEL6 Mask */

#define CMSDK_PWM_CTRL_PWM5_SEL_Pos 	3                                             /*!< CMSDK_PWM PCR: PWMSEL5 Position */
#define CMSDK_PWM_CTRL_PWM5_SEL_Msk 	(0x01ul << CMSDK_PWM_CTRL_PWM5_SEL_Pos)       /*!< CMSDK_PWM PCR: PWMSEL5 Mask */

#define CMSDK_PWM_CTRL_PWM4_SEL_Pos 	2                                             /*!< CMSDK_PWM PCR: PWMSEL4 Position */
#define CMSDK_PWM_CTRL_PWM4_SEL_Msk 	(0x01ul << CMSDK_PWM_CTRL_PWM4_SEL_Pos)       /*!< CMSDK_PWM PCR: PWMSEL4 Mask */

#define CMSDK_PWM_CTRL_PWM3_SEL_Pos 	1                                             /*!< CMSDK_PWM PCR: PWMSEL3 Position */
#define CMSDK_PWM_CTRL_PWM3_SEL_Msk 	(0x01ul << CMSDK_PWM_CTRL_PWM3_SEL_Pos)       /*!< CMSDK_PWM PCR: PWMSEL3 Mask */

#define CMSDK_PWM_CTRL_PWM2_SEL_Pos 	0                                             /*!< CMSDK_PWM PCR: PWMSEL2 Position */
#define CMSDK_PWM_CTRL_PWM2_SEL_Msk 	(0x01ul << CMSDK_PWM_CTRL_PWM2_SEL_Pos)       /*!< CMSDK_PWM PCR: PWMSEL2 Mask */

#define CMSDK_PWM_LOAD_ML6_EN_Pos 	6                                             /*!< CMSDK_PWM LER: PWMML6_EN Position */
#define CMSDK_PWM_LOAD_ML6_EN_Msk 	(0x01ul << CMSDK_PWM_LOAD_ML6_EN_Pos)         /*!< CMSDK_PWM LER: PWMML6_EN Mask */

#define CMSDK_PWM_LOAD_ML5_EN_Pos 	5                                             /*!< CMSDK_PWM LER: PWMML5_EN Position */
#define CMSDK_PWM_LOAD_ML5_EN_Msk 	(0x01ul << CMSDK_PWM_LOAD_ML5_EN_Pos)         /*!< CMSDK_PWM LER: PWMML5_EN Mask */

#define CMSDK_PWM_LOAD_ML4_EN_Pos 	4                                             /*!< CMSDK_PWM LER: PWMML4_EN Position */
#define CMSDK_PWM_LOAD_ML4_EN_Msk 	(0x01ul << CMSDK_PWM_LOAD_ML4_EN_Pos)         /*!< CMSDK_PWM LER: PWMML4_EN Mask */

#define CMSDK_PWM_LOAD_ML3_EN_Pos 	3                                             /*!< CMSDK_PWM LER: PWMML3_EN Position */
#define CMSDK_PWM_LOAD_ML3_EN_Msk 	(0x01ul << CMSDK_PWM_LOAD_ML3_EN_Pos)         /*!< CMSDK_PWM LER: PWMML3_EN Mask */

#define CMSDK_PWM_LOAD_ML2_EN_Pos 	2                                             /*!< CMSDK_PWM LER: PWMML2_EN Position */
#define CMSDK_PWM_LOAD_ML2_EN_Msk 	(0x01ul << CMSDK_PWM_LOAD_ML2_EN_Pos)         /*!< CMSDK_PWM LER: PWMML2_EN Mask */

#define CMSDK_PWM_LOAD_ML1_EN_Pos 	1                                             /*!< CMSDK_PWM LER: PWMML1_EN Position */
#define CMSDK_PWM_LOAD_ML1_EN_Msk 	(0x01ul << CMSDK_PWM_LOAD_ML1_EN_Pos)         /*!< CMSDK_PWM LER: PWMML1_EN Mask */

#define CMSDK_PWM_LOAD_ML0_EN_Pos 	0                                             /*!< CMSDK_PWM LER: PWMML0_EN Position */
#define CMSDK_PWM_LOAD_ML0_EN_Msk 	(0x01ul << CMSDK_PWM_LOAD_ML0_EN_Pos)         /*!< CMSDK_PWM LER: PWMML0_EN Mask */
/*@}*/ /* end of group CMSDK_PWM */


/*----------------------------- Timer (TIMER) -------------------------------*/
/** @addtogroup CMSDK_TIMER CMSDK Timer
  @{
*/
typedef struct
{
  __IO   uint32_t  CTRL;          /*!< Offset: 0x000 Control Register (R/W) */
  __IO   uint32_t  VALUE;         /*!< Offset: 0x004 Current Value Register (R/W) */
  __IO   uint32_t  RELOAD;        /*!< Offset: 0x008 Reload Value Register  (R/W) */
  union {
    __I    uint32_t  INTSTATUS;   /*!< Offset: 0x00C Interrupt Status Register (R/ ) */
    __O    uint32_t  INTCLEAR;    /*!< Offset: 0x00C Interrupt Clear Register ( /W) */
    };

} CMSDK_TIMER_TypeDef;

/* CMSDK_TIMER CTRL Register Definitions */

#define CMSDK_TIMER_CTRL_IRQEN_Pos          3                                              /*!< CMSDK_TIMER CTRL: IRQEN Position */
#define CMSDK_TIMER_CTRL_IRQEN_Msk          (0x01ul << CMSDK_TIMER_CTRL_IRQEN_Pos)         /*!< CMSDK_TIMER CTRL: IRQEN Mask */

#define CMSDK_TIMER_CTRL_SELEXTCLK_Pos      2                                              /*!< CMSDK_TIMER CTRL: SELEXTCLK Position */
#define CMSDK_TIMER_CTRL_SELEXTCLK_Msk      (0x01ul << CMSDK_TIMER_CTRL_SELEXTCLK_Pos)     /*!< CMSDK_TIMER CTRL: SELEXTCLK Mask */

#define CMSDK_TIMER_CTRL_SELEXTEN_Pos       1                                              /*!< CMSDK_TIMER CTRL: SELEXTEN Position */
#define CMSDK_TIMER_CTRL_SELEXTEN_Msk       (0x01ul << CMSDK_TIMER_CTRL_SELEXTEN_Pos)      /*!< CMSDK_TIMER CTRL: SELEXTEN Mask */

#define CMSDK_TIMER_CTRL_EN_Pos             0                                              /*!< CMSDK_TIMER CTRL: EN Position */
#define CMSDK_TIMER_CTRL_EN_Msk             (0x01ul << CMSDK_TIMER_CTRL_EN_Pos)            /*!< CMSDK_TIMER CTRL: EN Mask */

#define CMSDK_TIMER_VAL_CURRENT_Pos         0                                              /*!< CMSDK_TIMER VALUE: CURRENT Position */
#define CMSDK_TIMER_VAL_CURRENT_Msk         (0xFFFFFFFFul << CMSDK_TIMER_VAL_CURRENT_Pos)  /*!< CMSDK_TIMER VALUE: CURRENT Mask */

#define CMSDK_TIMER_RELOAD_VAL_Pos          0                                              /*!< CMSDK_TIMER RELOAD: RELOAD Position */
#define CMSDK_TIMER_RELOAD_VAL_Msk          (0xFFFFFFFFul << CMSDK_TIMER_RELOAD_VAL_Pos)   /*!< CMSDK_TIMER RELOAD: RELOAD Mask */

#define CMSDK_TIMER_INTSTATUS_Pos           0                                              /*!< CMSDK_TIMER INTSTATUS: INTSTATUSPosition */
#define CMSDK_TIMER_INTSTATUS_Msk           (0x01ul << CMSDK_TIMER_INTSTATUS_Pos)          /*!< CMSDK_TIMER INTSTATUS: INTSTATUSMask */

#define CMSDK_TIMER_INTCLEAR_Pos            0                                              /*!< CMSDK_TIMER INTCLEAR: INTCLEAR Position */
#define CMSDK_TIMER_INTCLEAR_Msk            (0x01ul << CMSDK_TIMER_INTCLEAR_Pos)           /*!< CMSDK_TIMER INTCLEAR: INTCLEAR Mask */

/*@}*/ /* end of group CMSDK_TIMER */


/*----------------------------- Dual Timer (DUALTIMER) -------------------------------*/
/** @addtogroup CMSDK_DUALTIMER Dual Timer
  @{
*/
typedef struct
{
  __IO   uint32_t  TimerLoad;          /*!< Offset: 0x000 Timer1 load Register (R/W) */
  __I    uint32_t  TimerValue;         /*!< Offset: 0x004 Timer1 Current Value Register (R/) */
  __IO   uint32_t  TimerControl;       /*!< Offset: 0x008 Timer1 Control Register  (R/W) */
  __O    uint32_t  TimerIntClr;        /*!< Offset: 0x00C Timer1 Interrupt Clear Register  (/W) */
  __I    uint32_t  TimerRIS;           /*!< Offset: 0x010 Timer1 Raw Interrupt Status Register (R/) */
  __I    uint32_t  TimerMIS;           /*!< Offset: 0x014 Timer1 Masked Interrupt Status Register (R/) */
  __IO   uint32_t  TimerBGLoad;        /*!< Offset: 0x018 Timer1 Background load Register (R/W) */ 

} CMSDK_DUALTIMER_TypeDef;

/* CMSDK_DUALTIMER Register Definitions */
#define CMSDK_DUALTIMER_LOAD_Pos             0                                               /*!< CMSDK_DUALTIMER LOAD: LOAD Position */
#define CMSDK_DUALTIMER_LOAD_Msk             (0xFFFFFFFFul << CMSDK_DUALTIMER_LOAD_Pos)      /*!< CMSDK_DUALTIMER LOAD: LOAD Mask */

#define CMSDK_DUALTIMER_VALUE_Pos            0                                               /*!< CMSDK_DUALTIMER VALUE: VALUE Position */
#define CMSDK_DUALTIMER_VALUE_Msk            (0xFFFFFFFFul << CMSDK_DUALTIMER_VALUE_Pos)     /*!< CMSDK_DUALTIMER VALUE: VALUE Mask */

#define CMSDK_DUALTIMER_CTRL_EN_Pos          7                                               /*!< CMSDK_DUALTIMER CTRL_EN: CTRL Enable Position */
#define CMSDK_DUALTIMER_CTRL_EN_Msk          (0x1ul << CMSDK_DUALTIMER_CTRL_EN_Pos)          /*!< CMSDK_DUALTIMER CTRL_EN: CTRL Enable Mask */

#define CMSDK_DUALTIMER_CTRL_MODE_Pos        6                                               /*!< CMSDK_DUALTIMER CTRL_MODE: CTRL MODE Position */
#define CMSDK_DUALTIMER_CTRL_MODE_Msk        (0x1ul << CMSDK_DUALTIMER_CTRL_MODE_Pos)        /*!< CMSDK_DUALTIMER CTRL_MODE: CTRL MODE Mask */

#define CMSDK_DUALTIMER_CTRL_INTEN_Pos       5                                               /*!< CMSDK_DUALTIMER CTRL_INTEN: CTRL Int Enable Position */
#define CMSDK_DUALTIMER_CTRL_INTEN_Msk       (0x1ul << CMSDK_DUALTIMER_CTRL_INTEN_Pos)       /*!< CMSDK_DUALTIMER CTRL_INTEN: CTRL Int Enable Mask */

#define CMSDK_DUALTIMER_CTRL_PRESCALE_Pos    2                                               /*!< CMSDK_DUALTIMER CTRL_PRESCALE: CTRL PRESCALE Position */
#define CMSDK_DUALTIMER_CTRL_PRESCALE_Msk    (0x3ul << CMSDK_DUALTIMER_CTRL_PRESCALE_Pos)    /*!< CMSDK_DUALTIMER CTRL_PRESCALE: CTRL PRESCALE Mask */

#define CMSDK_DUALTIMER_CTRL_SIZE_Pos        1                                               /*!< CMSDK_DUALTIMER CTRL_SIZE: CTRL SIZE Position */
#define CMSDK_DUALTIMER_CTRL_SIZE_Msk        (0x1ul << CMSDK_DUALTIMER_CTRL_SIZE_Pos)        /*!< CMSDK_DUALTIMER CTRL_SIZE: CTRL SIZE Mask */

#define CMSDK_DUALTIMER_CTRL_ONESHOT_Pos     0                                               /*!< CMSDK_DUALTIMER CTRL_ONESHOT: CTRL ONESHOT Position */
#define CMSDK_DUALTIMER_CTRL_ONESHOT_Msk     (0x1ul << CMSDK_DUALTIMER_CTRL_ONESHOT_Pos)     /*!< CMSDK_DUALTIMER CTRL_ONESHOT: CTRL ONESHOT Mask */

#define CMSDK_DUALTIMER_INTCLR_Pos           0                                               /*!< CMSDK_DUALTIMER INTCLR: INT Clear Position */
#define CMSDK_DUALTIMER_INTCLR_Msk           (0x1ul << CMSDK_DUALTIMER_INTCLR_Pos)           /*!< CMSDK_DUALTIMER INTCLR: INT Clear  Mask */

#define CMSDK_DUALTIMER_RAWINTSTAT_Pos       0                                               /*!< CMSDK_DUALTIMER RAWINTSTAT: Raw Int Status Position */
#define CMSDK_DUALTIMER_RAWINTSTAT_Msk       (0x1ul << CMSDK_DUALTIMER_RAWINTSTAT_Pos)       /*!< CMSDK_DUALTIMER RAWINTSTAT: Raw Int Status Mask */

#define CMSDK_DUALTIMER_MASKINTSTAT_Pos      0                                               /*!< CMSDK_DUALTIMER MASKINTSTAT: Mask Int Status Position */
#define CMSDK_DUALTIMER_MASKINTSTAT_Msk      (0x1ul << CMSDK_DUALTIMER_MASKINTSTAT_Pos)      /*!< CMSDK_DUALTIMER MASKINTSTAT: Mask Int Status Mask */

#define CMSDK_DUALTIMER_BGLOAD_Pos           0                                               /*!< CMSDK_DUALTIMER BGLOAD: Background Load Position */
#define CMSDK_DUALTIMER_BGLOAD_Msk           (0xFFFFFFFFul << CMSDK_DUALTIMER_BGLOAD_Pos)    /*!< CMSDK_DUALTIMER BGLOAD: Background Load Mask */
/*@}*/ /* end of group CMSDK_DUALTIMER */


/*-------------------- Real Time Clock (RTC) -------------------*/

/** @addtogroup CMSDK_RTC RTC
  @{
*/
typedef struct
{
  __IO   uint32_t  TR;    	    /*!< Offset: 0x000 RTC Time Register (R/W) */
  __IO   uint32_t  DR;    	    /*!< Offset: 0x004 RTC Date Register (R/W) */  
  __IO   uint32_t  TAR;             /*!< Offset: 0x008 RTC Time Alarm Register  (R/W) */
  __IO   uint32_t  DAR;             /*!< Offset: 0x00C RTC Date Alarm Register  (R/W) */
  __IO   uint32_t  IER;             /*!< Offset: 0x010 Interrupt Enable Register  (R/W) */
  union {
    __I    uint32_t  INTSTATUS;     /*!< Offset: 0x014 Interrupt Status Register  (R/) */
    __O    uint32_t  INTCLEAR;      /*!< Offset: 0x014 Interrupt Clear Register ( /W) */
    };
  __IO   uint32_t  CR;     	    /*!< Offset: 0x018 RTC Control Register  (R/W) */
  __IO   uint32_t  PR;      	    /*!< Offset: 0x01C RTC Prescaler Register  (R/W) */
  __IO   uint32_t  WTR;     	    /*!< Offset: 0x020 RTC Wakeup Timer Register  (R/W) */
  __IO   uint32_t  WPR;     	    /*!< Offset: 0x024 RTC Wakeup Prescaler Register  (R/W) */
  __I    uint32_t  SR;     	    /*!< Offset: 0x028 RTC Status Register  (R/) */

} CMSDK_RTC_TypeDef;


/* CMSDK_RTC  Register Definitions */
#define CMSDK_RTC_WUT_CLK_EN_Pos 		5                                              	/*!< CMSDK_RTC CR: WUT_CLK_EN Position */
#define CMSDK_RTC_WUT_CLK_EN_Msk 		(0x01ul << CMSDK_RTC_WUT_CLK_EN_Pos)         	/*!< CMSDK_RTC CR: WUT_CLK_EN Mask */

#define CMSDK_RTC_WUT_EN_Pos 			4                                             	/*!< CMSDK_RTC CR: WUT_EN Position */
#define CMSDK_RTC_WUT_EN_Msk 			(0x01ul << CMSDK_RTC_WUT_EN_Pos)           	/*!< CMSDK_RTC CR: WUT_EN Mask */

#define CMSDK_RTC_ALARM_EN_Pos 			3                                              	/*!< CMSDK_RTC CR: ALARM_EN Position */
#define CMSDK_RTC_ALARM_EN_Msk 			(0x01ul << CMSDK_RTC_ALARM_EN_Pos)           	/*!< CMSDK_RTC CR: ALARM_EN Mask */

#define CMSDK_RTC_DATA_MODE_Pos 		2                                             	/*!< CMSDK_RTC CR: DATA_MODE Position */
#define CMSDK_RTC_DATA_MODE_Msk 		(0x01ul << CMSDK_RTC_DATA_MODE_Pos)           	/*!< CMSDK_RTC CR: DATA_MODE Mask */

#define CMSDK_RTC_HOUR_MODE_Pos 		1                                              	/*!< CMSDK_RTC CR: HOUR_MODE Position */
#define CMSDK_RTC_HOUR_MODE_Msk 		(0x01ul << CMSDK_RTC_HOUR_MODE_Pos)           	/*!< CMSDK_RTC CR: HOUR_MODE Mask */

#define CMSDK_RTC_INIT_EN_Pos 			0                                              	/*!< CMSDK_RTC CR: INIT_EN Position */
#define CMSDK_RTC_INIT_EN_Msk 			(0x01ul << CMSDK_RTC_INIT_EN_Pos)           	/*!< CMSDK_RTC CR: INIT_EN Mask */

#define CMSDK_RTC_WUT_IE_Pos 			1                                             	/*!< CMSDK_RTC IER: WUT_IE Position */
#define CMSDK_RTC_WUT_IE_Msk 			(0x01ul << CMSDK_RTC_WUT_IE_Pos)           	/*!< CMSDK_RTC IER: WUT_IE Mask */

#define CMSDK_RTC_ALARM_IE_Pos 			0                                              	/*!< CMSDK_RTC IER: ALARM_IE Position */
#define CMSDK_RTC_ALARM_IE_Msk 			(0x01ul << CMSDK_RTC_ALARM_IE_Pos)           	/*!< CMSDK_RTC IER: ALARM_IE Mask */

#define CMSDK_RTC_WUT_VAL_SYNC_READY_Pos 	3                                             	/*!< CMSDK_RTC SR: WUT_VAL_SYNC_READY Position */
#define CMSDK_RTC_WUT_VAL_SYNC_READY_Msk 	(0x01ul << CMSDK_RTC_WUT_VAL_SYNC_READY_Pos)    /*!< CMSDK_RTC SR: WUT_VAL_SYNC_READY Mask */

#define CMSDK_RTC_WUT_PRES_SYNC_READY_Pos 	2                                              	/*!< CMSDK_RTC SR: WUT_PRES_SYNC_READY Position */
#define CMSDK_RTC_WUT_PRES_SYNC_READY_Msk 	(0x01ul << CMSDK_RTC_WUT_PRES_SYNC_READY_Pos)   /*!< CMSDK_RTC SR: WUT_PRES_SYNC_READY Mask */

#define CMSDK_RTC_PRES_SYNC_READY_Pos 		1                                              	/*!< CMSDK_RTC SR: RTC PRES_SYNC_READY Position */
#define CMSDK_RTC_PRES_SYNC_READY_Msk 		(0x01ul << CMSDK_RTC_PRES_SYNC_READY_Pos)       /*!< CMSDK_RTC SR: RTC PRES_SYNC_READY Mask */

#define CMSDK_RTC_INIT_SYNC_READY_Pos 		0                                              	/*!< CMSDK_RTC SR: INIT_SYNC_READY Position */
#define CMSDK_RTC_INIT_SYNC_READY_Msk 		(0x01ul << CMSDK_RTC_INIT_SYNC_READY_Pos)       /*!< CMSDK_RTC SR: INIT_SYNC_READY Mask */
/*@}*/ /* end of group CMSDK_RTC */


/*-------------------- General Purpose Input Output (GPIO) -------------------*/

/** @addtogroup CMSDK_GPIO CMSDK GPIO
  @{
*/
typedef struct
{
  __I    uint32_t  DATAIN;           /*!< Offset: 0x000 Data input Register (R/) */
  __IO   uint32_t  DATAOUT;          /*!< Offset: 0x004 Data Output Register (R/W) */
  __IO   uint32_t  BITSET;     	     /*!< Offset: 0x008 DataOut Bit Set Register  (R/W) */
  __IO   uint32_t  BITCLR;           /*!< Offset: 0x00C DataOut Bit Clear Register  (R/W) */
  __IO   uint32_t  OE;               /*!< Offset: 0x010 Output Enable Register  (R/W) */
  __IO   uint32_t  IE;               /*!< Offset: 0x014 Input Enable Register  (R/W) */
  __IO   uint32_t  PU;               /*!< Offset: 0x018 Pull Up Register  (R/W) */
  __IO   uint32_t  PD;               /*!< Offset: 0x01C Pull Down Register  (R/W) */
  __IO   uint32_t  CS;               /*!< Offset: 0x020 CMOS/Schmitt Input Type Register  (R/W) */
  __IO   uint32_t  SL;               /*!< Offset: 0x024 Slew Rate Register  (R/W) */
  __IO   uint32_t  OPDRV0;           /*!< Offset: 0x028 Output Drive Strength 0 Register  (R/W) */
  __IO   uint32_t  OPDRV1;           /*!< Offset: 0x02C Output Drive Strength 1 Register  (R/W) */
  __IO   uint32_t  ODEN;             /*!< Offset: 0x030 Open drain Enable Register  (R/W) */
  __IO   uint32_t  ALTFL;            /*!< Offset: 0x034 Alternate function select Register  (R/W) */
  __IO   uint32_t  ALTFH;            /*!< Offset: 0x038 Alternate function select Register  (R/W) */
  __IO   uint32_t  ANAEN;            /*!< Offset: 0x03C Analog Channel Enable Register  (R/W) */
} CMSDK_GPIO_TypeDef;

#define CMSDK_GPIO_DATA_Pos            0                                          /*!< CMSDK_GPIO DATAIN: DATAIN Position */
#define CMSDK_GPIO_DATA_Msk            (0xFFFFFFul << CMSDK_GPIO_DATA_Pos)        /*!< CMSDK_GPIO DATAIN: DATAIN Mask */

#define CMSDK_GPIO_DATAOUT_Pos         0                                          /*!< CMSDK_GPIO DATAOUT: DATAOUT Position */
#define CMSDK_GPIO_DATAOUT_Msk         (0xFFFFFFul << CMSDK_GPIO_DATAOUT_Pos)     /*!< CMSDK_GPIO DATAOUT: DATAOUT Mask */


/*@}*/ /* end of group CMSDK_GPIO */


/*------------- System Control (SYSCON) --------------------------------------*/
/** @addtogroup CMSDK_SYSCON CMSDK System Control
  @{
*/
typedef struct
{
  __IO   uint32_t  CLK_CFG;          /*!< Offset: 0x000 Clock Configuration Register (R/W) */
  __IO   uint32_t  HSI_CTRL;         /*!< Offset: 0x004 HSI Control Register (R/W) */
  __IO   uint32_t  LSI_CTRL;         /*!< Offset: 0x008 LSI Control Register  (R/W) */
  __IO   uint32_t  LSE_CTRL;         /*!< Offset: 0x00C LSE Control Register  (R/W) */
  __IO   uint32_t  AHB_CLKEN;        /*!< Offset: 0x010 AHB Peripheral Clock Enable Register (R/W) */
  __IO   uint32_t  APB_CLKEN;        /*!< Offset: 0x014 APB Peripheral Clock Enable Register (R/W) */
  __IO   uint32_t  PERI_CLKEN;       /*!< Offset: 0x018 Peripheral Working Clock Enable Register (R/W) */
  __IO   uint32_t  SLP_PCLKEN;       /*!< Offset: 0x01C APB Peripheral Clock Enable in Sleep/Stop mode Register (R/W) */
  __IO   uint32_t  RST_CTRL;         /*!< Offset: 0x020 Reset Control Register (R/W) */
  union {
    __I    uint32_t  RST_FLAG_STS;   /*!< Offset: 0x024 Reset Flag Status Register (R/) */
    __O    uint32_t  RST_FLAG_CLR;   /*!< Offset: 0x024 Reset Flag Clear Register (/W) */
    };
  __O    uint32_t  PRST_KEY;         /*!< Offset: 0x028 Peripheral Reset Enable Key Register (/W) */
  __IO   uint32_t  AHB_RST;          /*!< Offset: 0x02C AHB Peripheral Reset Register (R/W) */
  __IO   uint32_t  APB_RST;          /*!< Offset: 0x030 APB Peripheral Reset Register (R/W) */
  __IO   uint32_t  SYS_CFG;          /*!< Offset: 0x034 System Configuration Register (R/W) */
  __IO   uint32_t  PMU_CTRL;         /*!< Offset: 0x038 PMU Control Register (R/W) */
} CMSDK_SYSCON_TypeDef;

#define CMSDK_SYSCON_SYSCLK_SEL_Pos            0
#define CMSDK_SYSCON_SYSCLK_SEL_Msk            (0x03ul << CMSDK_SYSCON_SYSCLK_SEL_Pos)          /*!< CMSDK_SYSCON CLK_CFG: SYSCLK_SEL Mask */

#define CMSDK_SYSCON_SYSCLK_SWSTS_Pos          2
#define CMSDK_SYSCON_SYSCLK_SWSTS_Msk          (0x03ul << CMSDK_SYSCON_SYSCLK_SWSTS_Pos)        /*!< CMSDK_SYSCON CLK_CFG: SYSCLK_SWSTS Mask */

#define CMSDK_SYSCON_LFCLK_SEL_Pos             4	
#define CMSDK_SYSCON_LFCLK_SEL_Msk             (0x01ul << CMSDK_SYSCON_LFCLK_SEL_Pos)           /*!< CMSDK_SYSCON CLK_CFG: LFCLK_SEL Mask */

#define CMSDK_SYSCON_LFCLK_SWSTS_Pos           5
#define CMSDK_SYSCON_LFCLK_SWSTS_Msk           (0x01ul << CMSDK_SYSCON_LFCLK_SWSTS_Pos)         /*!< CMSDK_SYSCON CLK_CFG: LFCLK_SWSTS Mask */

#define CMSDK_SYSCON_AHB_PRES_Pos              8
#define CMSDK_SYSCON_AHB_PRES_Msk              (0x07ul << CMSDK_SYSCON_AHB_PRES_Pos)            /*!< CMSDK_SYSCON CLK_CFG: AHB_PRES Mask */

#define CMSDK_SYSCON_APB_PRES_Pos              12
#define CMSDK_SYSCON_APB_PRES_Msk              (0x07ul << CMSDK_SYSCON_APB_PRES_Pos)            /*!< CMSDK_SYSCON CLK_CFG: APB_PRES Mask */

#define CMSDK_SYSCON_MCO_SEL_Pos               16
#define CMSDK_SYSCON_MCO_SEL_Msk               (0x03ul << CMSDK_SYSCON_MCO_SEL_Pos)             /*!< CMSDK_SYSCON CLK_CFG: MCO_SEL Mask */

#define CMSDK_SYSCON_HSI_EN_Pos                0
#define CMSDK_SYSCON_HSI_EN_Msk                (0x01ul << CMSDK_SYSCON_HSI_EN_Pos)              /*!< CMSDK_SYSCON HSI_CTRL: HSI_FREQ Mask */

#define CMSDK_SYSCON_HSI_FREQ_Pos              4
#define CMSDK_SYSCON_HSI_FREQ_Msk              (0x03ul << CMSDK_SYSCON_HSI_FREQ_Pos)            /*!< CMSDK_SYSCON HSI_CTRL: HSI_FREQ Mask */

#define CMSDK_SYSCON_LSI_EN_Pos                0
#define CMSDK_SYSCON_LSI_EN_Msk                (0x01ul << CMSDK_SYSCON_LSI_EN_Pos)              /*!< CMSDK_SYSCON LSI_CTRL: LSI_EN Mask */

#define CMSDK_SYSCON_LSE_EN_Pos                0
#define CMSDK_SYSCON_LSE_EN_Msk                (0x03ul << CMSDK_SYSCON_LSE_EN_Pos)              /*!< CMSDK_SYSCON LSE_CTRL: LSE_EN Mask */

#define CMSDK_SYSCON_AHB_PORT_CLKEN_Pos        0
#define CMSDK_SYSCON_AHB_PORT_CLKEN_Msk        (0x07ul << CMSDK_SYSCON_AHB_PORT_CLKEN_Pos)      /*!< CMSDK_SYSCON AHB Peri CLK_EN : AHB_PORT_CLKCEN */

#define CMSDK_SYSCON_APB_PORT_CLKEN_Pos        0
#define CMSDK_SYSCON_APB_PORT_CLKEN_Msk        (0xFFul << CMSDK_SYSCON_APB_PORT_CLKEN_Pos)      /*!< CMSDK_SYSCON APB Peri CLK_EN: APB_PORT_CLKEN */

#define CMSDK_SYSCON_RTC_CLKEN_Pos             0
#define CMSDK_SYSCON_RTC_CLKEN_Msk             (0x01ul << CMSDK_SYSCON_RTC_CLKEN_Pos)           /*!< CMSDK_SYSCON Peri Working CLK_EN: RTC_CLKEN Mask */

#define CMSDK_SYSCON_APB_SLP_CLKEN_Pos         0
#define CMSDK_SYSCON_APB_SLP_CLKEN_Msk         (0xFFul << CMSDK_SYSCON_APB_SLP_CLKEN_Pos)       /*!< CMSDK_SYSCON Peri APB CLK_EN in sleep/stop mode: APB_SLP_CLKEN Mask */

#define CMSDK_SYSCON_LOCKUP_RESETEN_Pos        0
#define CMSDK_SYSCON_LOCKUP_RESETEN_Msk        (0x01ul << CMSDK_SYSCON_LOCKUP_RESETEN_Pos)      /*!< CMSDK_SYSCON RST_CTRL: LOCKUP_RESETN Mask */

#define CMSDK_SYSCON_PORRST_FLAG_Pos           0
#define CMSDK_SYSCON_PORRST_FLAG_Msk           (0x01ul << CMSDK_SYSCON_PORRST_FLAG_Pos)         /*!< CMSDK_SYSCON RST_FLAG_STS: PORRST_FLAG Mask */

#define CMSDK_SYSCON_WDTRST_FLAG_Pos           2
#define CMSDK_SYSCON_WDTRST_FLAG_Msk           (0x01ul << CMSDK_SYSCON_WDTRST_FLAG_Pos)         /*!< CMSDK_SYSCON RST_FLAG_STS: WDTRST_FLAG Mask */

#define CMSDK_SYSCON_SOFTRST_FLAG_Pos          1
#define CMSDK_SYSCON_SOFTRST_FLAG_Msk          (0x01ul << CMSDK_SYSCON_SOFTRST_FLAG_Pos)        /*!< CMSDK_SYSCON RST_FLAG_STS: SOFTRST_FLAG Mask */

#define CMSDK_SYSCON_LOCKUPRST_FLAG_Pos        3 
#define CMSDK_SYSCON_LOCKUPRST_FLAG_Msk        (0x01ul << CMSDK_SYSCON_LOCKUPRST_FLAG_Pos)      /*!< CMSDK_SYSCON RST_FLAG_STS: LOCKUPRST_FLAG Mask */

#define CMSDK_SYSCON_PINRST_FLAG_Pos           4
#define CMSDK_SYSCON_PINRST_FLAG_Msk           (0x01ul << CMSDK_SYSCON_PINRST_FLAG_Pos)         /*!< CMSDK_SYSCON RST_FLAG_STS: PINRST_FLAG Mask */

#define CMSDK_SYSCON_PINTEST_FLAG_Pos          5
#define CMSDK_SYSCON_PINTEST_FLAG_Msk          (0x01ul << CMSDK_SYSCON_PINTEST_FLAG_Pos)        /*!< CMSDK_SYSCON RST_FLAG_STS: PINTEST_FLAG Mask */

#define CMSDK_SYSCON_PRST_KEY_Pos              0
#define CMSDK_SYSCON_PRST_KEY_Msk              (0xFFFFul << CMSDK_SYSCON_PRST_KEY_Pos)          /*!< CMSDK_SYSCON Peri RST_ENKEY: PRST_KEY Mask */

#define CMSDK_SYSCON_AHB_PORT_RST_Pos          0  
#define CMSDK_SYSCON_AHB_PORT_RST_Msk          (0x07ul << CMSDK_SYSCON_AHB_PORT_RST_Pos)        /*!< CMSDK_SYSCON AHB Peri RST_Reg: AHB_PORT_RST Mask */

#define CMSDK_SYSCON_APB_PORT_RST_Pos          0
#define CMSDK_SYSCON_APB_PORT_RST_Msk          (0xFFul << CMSDK_SYSCON_APB_PORT_RST_Pos)        /*!< CMSDK_SYSCON APB Peri RST_Reg: APB_PORT_RST Mask */

#define CMSDK_SYSCON_BOOT_MODE_Pos             8
#define CMSDK_SYSCON_BOOT_MODE_Msk             (0x03ul << CMSDK_SYSCON_BOOT_MODE_Pos)           /*!< CMSDK_SYSCON SYS_CFG: BOOT_MODE Mask */

#define CMSDK_SYSCON_REMAP_Pos                 0
#define CMSDK_SYSCON_REMAP_Msk                 (0x03ul << CMSDK_SYSCON_REMAP_Pos)               /*!< CMSDK_SYSCON SYS_CFG: REMAP Mask */

//#define CMSDK_SYSCON_BG_STPDIS_Pos           0
//#define CMSDK_SYSCON_BG_STPDIS_Msk           (0x01ul << CMSDK_SYSCON_BG_STPDIS_Pos)           /*!< CMSDK_SYSCON PMU_CTRL: BG_STPDI Mask */

#define CMSDK_SYSCON_WKFREQ_SEL_Pos            0
#define CMSDK_SYSCON_WKFREQ_SEL_Msk            (0x01ul << CMSDK_SYSCON_WKFREQ_SEL_Pos)          /*!< CMSDK_SYSCON PMU_CTRL: WKFREQ_SEL Mask */

#define CMSDK_SYSCON_WKUP_DLY_Pos              4
#define CMSDK_SYSCON_WKUP_DLY_Msk              (0x03ul << CMSDK_SYSCON_WKUP_DLY_Pos)            /*!< CMSDK_SYSCON PMU_CTRL: WKUP_DLY Mask */


/*@}*/ /* end of group CMSDK_SYSCON */

/*------------  Extended Interrupt and Event Controller  (EXTI) --------------------------------------*/
/** @addtogroup CMSDK_EXTI
  @{
*/
typedef struct
{
  __IO   uint32_t  RTSR;          	/*!< Offset: 0x000 Rising Trigger Selection Register (R/W) */
  __IO   uint32_t  FTSR;         	/*!< Offset: 0x004 Falling Trigger Selection Register (R/W) */
  __O    uint32_t  SWIER;         	/*!< Offset: 0x008 Software Interrupt Event Register  (/W) */
  union {
    __I    uint32_t  RISE_PEND_STS;   	/*!< Offset: 0x00C Rising Edge Pending Status Register (R/) */
    __O    uint32_t  RISE_PEND_CLR;   	/*!< Offset: 0x00C Rising Edge Pending Clear Register (/W) */
    };
  union {
    __I    uint32_t  FALL_PEND_STS;   	/*!< Offset: 0x010 Falling Edge Pending Status Register (R/) */
    __O    uint32_t  FALL_PEND_CLR;   	/*!< Offset: 0x010 Falling Edge Pending Clear Register (/W) */
    };
  __IO   uint32_t  IMR;        		/*!< Offset: 0x014 CPU Wakeup with Interrupt Mask Register (R/W) */
  __IO   uint32_t  EMR;       		/*!< Offset: 0x018 CPU Wakeup with Event Mask Register (R/W) */
} CMSDK_EXTI_TypeDef;


/*@}*/ /* end of group CMSDK_EXTI */

/*------------  MTP Controller  (MTP) --------------------------------------*/
/** @addtogroup CMSDK_MTP
  @{
*/
typedef struct
{
  __IO   uint32_t  MTP_CR;          	/*!< Offset: 0x000 MTP Controller Control register (R/W) */
  union {
    __I  uint32_t  MTP_SR;   		/*!< Offset: 0x004 MTP Controller Status Register (R/) */
    __O  uint32_t  MTP_CLR;   		/*!< Offset: 0x004 MTP Controller clear Register (/W) */
    };
  __IO  uint32_t  MTP_ACLR;        	/*!< Offset: 0x008 MTP Controller Application Code Lock Register (R/W) */
  __O   uint32_t  MTP_KEYR;       	/*!< Offset: 0x00C MTP Controller Key  Register(/W) */
  __IO  uint32_t  MTP_IER;		/*!< Offset: 0x010 MTP Controller Interrupt Enable register (R/W) */ 
  __I   uint32_t  MTP_UCR;		/*!< Offset: 0x014 MTP User Configuration register (R/) */ 
  __IO  uint32_t  MTP_INF_EPM_ADR;	/*!< Offset: 0x018 MTP Information EEPROM Address register (R/W) */
  __IO  uint32_t  MTP_INF_EPM_WDATA;	/*!< Offset: 0x01C MTP Information EEPROM Writing Data register (R/W) */ 
  __I   uint32_t  MTP_INF_EPM_RDATA;	/*!< Offset: 0x020 MTP Information EEPROM Reading Data register (R/) */
  __I   uint32_t  MTP_DBG;		/*!< Offset: 0x024 MTP Debug Data register (R/) */ 
  __I   uint32_t  MTP_DEVICE_ID1;	/*!< Offset: 0x028 MTP Device ID1 register (R/) */ 
  __I   uint32_t  MTP_DEVICE_ID2;	/*!< Offset: 0x02C MTP Device ID2 register (R/) */
  __I   uint32_t  MTP_DEVICE_ID3;	/*!< Offset: 0x030 MTP Device ID3 register (R/) */
  __IO  uint32_t  MTP_OSCA_FT;		/*!< Offset: 0x034 MTP OSCA_FT Trim register (R/W) */
  __IO  uint32_t  MTP_OSC32K_RTTRIM;	/*!< Offset: 0x038 MTP OSC32K_RTTRIM Trim register (R/W) */ 
  __IO  uint32_t  MTP_BG_TRIM; 		/*!< Offset: 0x03C MTP BG Trim register (R/W) */
} CMSDK_MTPREG_TypeDef;

#define CMSDK_MTPREG_MTP_INF_EPM_RDATA_Pos            0
#define CMSDK_MTPREG_MTP_INF_EPM_RDATA_Msk            (0x100ul << CMSDK_MTPREG_MTP_INF_EPM_RDATA_Pos)          /*!< CMSDK_MTPREG MTP_INF_EPM_RDATA: MTP_INF_EPM_RDATA Mask */

#define CMSDK_MTPREG_MTP_SR_KEY_STA_Pos               7
#define CMSDK_MTPREG_MTP_SR_KEY_STA_Msk               (0x03ul << CMSDK_MTPREG_MTP_INF_EPM_RDATA_Pos)          /*!< CMSDK_MTPREG MTP_INF_EPM_RDATA: MTP_INF_EPM_RDATA Mask */


/*@}*/ /* end of group CMSDK_MTPREG */

/*------------  LCD Controller  (LCD) --------------------------------------*/
/** @addtogroup CMSDK_LCD
  @{
*/
typedef struct
{
  __IO  uint32_t  LCD_CR;          	/*!< Offset: 0x000 LCD Controller LCD Control Register (R/W) */
  __IO  uint32_t  COM0_SEG;       	/*!< Offset: 0x004 LCD Controller LCD data buffer Registers(COM0) (R/W) */
  __IO  uint32_t  COM1_SEG;       	/*!< Offset: 0x008 LCD Controller LCD data buffer Registers(COM1) (R/W) */
  __IO  uint32_t  COM2_SEG;		/*!< Offset: 0x00C LCD Controller LCD data buffer Registers(COM2) (R/W) */ 
  __IO  uint32_t  COM3_SEG;		/*!< Offset: 0x010 LCD Controller LCD data buffer Registers(COM3) (R/W) */ 
  __IO  uint32_t  LCD_FCR;		/*!< Offset: 0x014 LCD Controller LCD Frequency Control Register (R/W) */
  __IO  uint32_t  LCD_FLKT;		/*!< Offset: 0x018 LCD Controller LCD Flick Time Register (R/W) */
  __IO  uint32_t  LCD_COM_EN;		/*!< Offset: 0x01C LCD Controller LCD COM Enable Register (R/W) */
  __IO  uint32_t  LCD_SEG_EN;		/*!< Offset: 0x020 LCD Controller LCD SEG Enable Register (R/W) */
  __IO  uint32_t  LCD_IER;		/*!< Offset: 0x024 LCD Controller LCD Interrupt Enable Register (R/W) */
  union {
    __I  uint32_t  LCD_ISR;   		/*!< Offset: 0x028 LCD Controller LCD Interrupt Status Register (R) */
    __O  uint32_t  LCD_INT_CLR;   	/*!< Offset: 0x028 LCD Controller LCD Interrupt Status Register (W) */
    }; 
  __IO  uint32_t  LCD_BIAS;	        /*!< Offset: 0x02C LCD Controller LCD BIAS Register */
} CMSDK_LCD_TypeDef;

#define CMSDK_LCD_ENABLE_Pos            	2
#define CMSDK_LCD_ENABLE_Msk            	(0x01ul << CMSDK_LCD_ENABLE_Pos)          	/*!< CMSDK_LCD_CR REG LCD_ENABLE: LCD ENABLE Mask */

#define CMSDK_LCD_FLICK_ENABLE_Pos            	14
#define CMSDK_LCD_FLICK_ENABLE_Msk            	(0x01ul << CMSDK_LCD_FLICK_ENABLE_Pos)          /*!< CMSDK_LCD_CR REG FLICK_ENABLE: LCD FLICK_ENABLE Mask */

#define CMSDK_LCD_FCR_DF_Pos            	0
#define CMSDK_LCD_FCR_DF_Msk            	(0xFFul << CMSDK_LCD_FCR_DF_Pos)          	/*!< CMSDK_LCD_FCR REG Display FREQ: LCD DISPLAY FREQ Mask */

#define CMSDK_LCD_FLKT_TON_Pos            	0
#define CMSDK_LCD_FLKT_TON_Msk            	(0xFFul << CMSDK_LCD_FLKT_TON_Pos)         	/*!< CMSDK_LCD_FLKT REG TON(display on time): CMSDK_LCD_FLK_TON Mask */

#define CMSDK_LCD_FLKT_TOFF_Pos            	8
#define CMSDK_LCD_FLKT_TOFF_Msk            	(0xFFul << CMSDK_LCD_FLKT_TOFF_Pos)         	/*!< CMSDK_LCD_FLKT REG TOFF(display off time): CMSDK_LCD_FLK_TOFF Mask */

#define CMSDK_LCD_IER_DONIE_Pos            	1
#define CMSDK_LCD_IER_DONIE_Msk            	(0x01ul << CMSDK_LCD_IER_DONIE_Pos)         	/*!< CMSDK_LCD_IER REG DONIE(DISP ON INTR EN): CMSDK_LCD_IER_DONIE Mask */

#define CMSDK_LCD_IER_DOFFIE_Pos            	0
#define CMSDK_LCD_IER_DOFFIE_Msk            	(0x01ul << CMSDK_LCD_IER_DOFFIE_Pos)         	/*!< CMSDK_LCD_IER REG DOFFIE(DISP OFF INTR EN): CMSDK_LCD_IER_DOFFIE Mask */

#define CMSDK_LCD_ISR_DOFFIF_Pos            	0
#define CMSDK_LCD_ISR_DOFFIF_Msk            	(0x01ul << CMSDK_LCD_ISR_DOFFIF_Pos)         	/*!< CMSDK_LCD_ISR REG DOFFIF(DISP OFF INTR FLAG): CMSDK_LCD_ISR_DOFFIF Mask */

#define CMSDK_LCD_ISR_DONIF_Pos            	1
#define CMSDK_LCD_ISR_DONIF_Msk            	(0x01ul << CMSDK_LCD_ISR_DONIF_Pos)         	/*!< CMSDK_LCD_ISR REG DONIF(DISP ON INTR FLAG): CMSDK_LCD_ISR_DONIF Mask */

#define CMSDK_LCD_DATA_Pos            		0
#define CMSDK_LCD_DATA_Msk            		(0xFFFFul << CMSDK_LCD_DATA_Pos)         	/*!< CMSDK_LCD_DATAx Buffer Register: CMSDK_LCD_DATA Mask */

#define CMSDK_LCD_COM_EN0_Pos            	0
#define CMSDK_LCD_COM_EN0_Msk            	(0x01ul << CMSDK_LCD_COM_EN0_Pos)         	/*!< CMSDK_LCD_COM_EN REG COM_EN0: CMSDK_LCD_COM_EN0 Mask */

#define CMSDK_LCD_COM_EN1_Pos            	1
#define CMSDK_LCD_COM_EN1_Msk            	(0x01ul << CMSDK_LCD_COM_EN1_Pos)         	/*!< CMSDK_LCD_COM_EN REG COM_EN1: CMSDK_LCD_COM_EN1 Mask */

#define CMSDK_LCD_COM_EN2_Pos            	2
#define CMSDK_LCD_COM_EN2_Msk            	(0x01ul << CMSDK_LCD_COM_EN2_Pos)         	/*!< CMSDK_LCD_COM_EN REG COM_EN2: CMSDK_LCD_COM_EN2 Mask */

#define CMSDK_LCD_COM_EN3_Pos            	3
#define CMSDK_LCD_COM_EN3_Msk            	(0x01ul << CMSDK_LCD_COM_EN3_Pos)         	/*!< CMSDK_LCD_COM_EN REG COM_EN3: CMSDK_LCD_COM_EN3 Mask */

#define CMSDK_LCD_SEG_EN0_Pos            	0
#define CMSDK_LCD_SEG_EN0_Msk            	(0x01ul << CMSDK_LCD_SEG_EN0_Pos)         	/*!< CMSDK_LCD_SEG_EN REG SEG_EN0: CMSDK_LCD_SEG_EN0 Mask */

#define CMSDK_LCD_SEG_EN1_Pos            	1
#define CMSDK_LCD_SEG_EN1_Msk            	(0x01ul << CMSDK_LCD_SEG_EN1_Pos)         	/*!< CMSDK_LCD_SEG_EN REG SEG_EN1: CMSDK_LCD_SEG_EN1 Mask */

#define CMSDK_LCD_SEG_EN2_Pos            	2
#define CMSDK_LCD_SEG_EN2_Msk            	(0x01ul << CMSDK_LCD_SEG_EN2_Pos)         	/*!< CMSDK_LCD_SEG_EN REG SEG_EN2: CMSDK_LCD_SEG_EN2 Mask */

#define CMSDK_LCD_SEG_EN3_Pos            	3
#define CMSDK_LCD_SEG_EN3_Msk            	(0x01ul << CMSDK_LCD_SEG_EN3_Pos)         	/*!< CMSDK_LCD_SEG_EN REG SEG_EN3: CMSDK_LCD_SEG_EN3 Mask */

#define CMSDK_LCD_SEG_EN4_Pos            	4
#define CMSDK_LCD_SEG_EN4_Msk            	(0x01ul << CMSDK_LCD_SEG_EN4_Pos)         	/*!< CMSDK_LCD_SEG_EN REG SEG_EN4: CMSDK_LCD_SEG_EN4 Mask */

#define CMSDK_LCD_SEG_EN5_Pos            	5
#define CMSDK_LCD_SEG_EN5_Msk            	(0x01ul << CMSDK_LCD_SEG_EN5_Pos)         	/*!< CMSDK_LCD_SEG_EN REG SEG_EN5: CMSDK_LCD_SEG_EN5 Mask */

#define CMSDK_LCD_SEG_EN6_Pos            	6
#define CMSDK_LCD_SEG_EN6_Msk            	(0x01ul << CMSDK_LCD_SEG_EN6_Pos)         	/*!< CMSDK_LCD_SEG_EN REG SEG_EN6: CMSDK_LCD_SEG_EN6 Mask */

#define CMSDK_LCD_SEG_EN7_Pos            	7
#define CMSDK_LCD_SEG_EN7_Msk            	(0x01ul << CMSDK_LCD_SEG_EN7_Pos)         	/*!< CMSDK_LCD_SEG_EN REG SEG_EN7: CMSDK_LCD_SEG_EN7 Mask */

#define CMSDK_LCD_SEG_EN8_Pos            	8
#define CMSDK_LCD_SEG_EN8_Msk            	(0x01ul << CMSDK_LCD_SEG_EN8_Pos)         	/*!< CMSDK_LCD_SEG_EN REG SEG_EN8: CMSDK_LCD_SEG_EN8 Mask */

#define CMSDK_LCD_SEG_EN9_Pos            	9
#define CMSDK_LCD_SEG_EN9_Msk            	(0x01ul << CMSDK_LCD_SEG_EN9_Pos)         	/*!< CMSDK_LCD_SEG_EN REG SEG_EN9: CMSDK_LCD_SEG_EN9 Mask */

#define CMSDK_LCD_SEG_EN10_Pos            	10
#define CMSDK_LCD_SEG_EN10_Msk            	(0x01ul << CMSDK_LCD_SEG_EN10_Pos)         	/*!< CMSDK_LCD_SEG_EN REG SEG_EN10: CMSDK_LCD_SEG_EN10 Mask */

#define CMSDK_LCD_SEG_EN11_Pos            	11
#define CMSDK_LCD_SEG_EN11_Msk            	(0x01ul << CMSDK_LCD_SEG_EN11_Pos)         	/*!< CMSDK_LCD_SEG_EN REG SEG_EN11: CMSDK_LCD_SEG_EN11 Mask */

#define CMSDK_LCD_SEG_EN12_Pos            	12
#define CMSDK_LCD_SEG_EN12_Msk            	(0x01ul << CMSDK_LCD_SEG_EN12_Pos)         	/*!< CMSDK_LCD_SEG_EN REG SEG_EN12: CMSDK_LCD_SEG_EN12 Mask */

#define CMSDK_LCD_SEG_EN13_Pos            	13
#define CMSDK_LCD_SEG_EN13_Msk            	(0x01ul << CMSDK_LCD_SEG_EN13_Pos)         	/*!< CMSDK_LCD_SEG_EN REG SEG_EN13: CMSDK_LCD_SEG_EN13 Mask */

#define CMSDK_LCD_SEG_EN14_Pos            	14
#define CMSDK_LCD_SEG_EN14_Msk            	(0x01ul << CMSDK_LCD_SEG_EN14_Pos)         	/*!< CMSDK_LCD_SEG_EN REG SEG_EN14: CMSDK_LCD_SEG_EN14 Mask */

#define CMSDK_LCD_SEG_EN15_Pos            	15
#define CMSDK_LCD_SEG_EN15_Msk            	(0x01ul << CMSDK_LCD_SEG_EN15_Pos)         	/*!< CMSDK_LCD_SEG_EN REG SEG_EN15: CMSDK_LCD_SEG_EN15 Mask */

/*@}*/ /* end of group CMSDK_LCD */

/*------------  ADC Controller  (ADC) --------------------------------------*/
/** @addtogroup CMSDK_ADC
  @{
*/
typedef struct
{
  __IO  uint32_t  ADC_CONFG;          	/*!< Offset: 0x000 ADC Controller ADC Configuration Register (R/W) */
  __IO  uint32_t  ADC_CTRL;       	/*!< Offset: 0x004 ADC Controller ADC Control Register (R/W) */
  __IO  uint32_t  ADC_IER;       	/*!< Offset: 0x008 ADC Controller ADC Interrupt Enable Register (R/W) */
    union {
    __I    uint32_t  ADC_ISR;   	/*!< Offset: 0x00C ADC Controller ADC Interrupt Status Register (R/) */
    __O    uint32_t  ADC_INT_CLR;   	/*!< Offset: 0x00C ADC Controller ADC Interrupt Clear Register (/W) */
    };  
  __I   uint32_t  ADC_SR;		/*!< Offset: 0x010 ADC Controller ADC Status Register (R) */ 
  __IO  uint32_t  ADC_CLK_DIV;		/*!< Offset: 0x014 ADC Controller ADC Clock Divider Register (R/W) */
  __IO  uint32_t  ADC_SAMP_TIME;	/*!< Offset: 0x018 ADC Controller ADC Sampling Time register (R/W) */
  __IO  uint32_t  ADC_DATA;		/*!< Offset: 0x01C ADC Controller ADC Data Register (R/W) */
  __IO  uint32_t  ADC_CH_SEL;		/*!< Offset: 0x020 ADC Controller ADC Channel Selection Register (R/W) */
  __IO  uint32_t  ADC_EOC_CONFG;	/*!< Offset: 0x024 ADC Controller ADC EOC_Configuration_Register (R/W) */ 
  } CMSDK_ADC_TypeDef;

#define CMSDK_ADC_COV_MODE_Pos            	0
#define CMSDK_ADC_COV_MODE_Msk            	(0x01ul << CMSDK_ADC_COV_MODE_Pos)          	/*!< CMSDK_ADC_CONFIG REG ADC conversion mode: ADC conversion mode Mask */

#define CMSDK_ADC_OVERRUN_MODE_Pos		1 
#define CMSDK_ADC_OVERRUN_MODE_Msk		(0x01ul << CMSDK_ADC_OVERRUN_MODE_Pos)         	/*!< CMSDK_ADC_CONFIG REG ADC overrun mode enable: ADC overrun mode enable Mask */

#define CMSDK_WAIT_MODE_EN_Pos			2 
#define CMSDK_WAIT_MODE_EN_Msk			(0x01ul << CMSDK_WAIT_MODE_EN_Pos)         	/*!< CMSDK_ADC_CONFIG REG ADC WAIT mode enable: ADC WAIT mode enable Mask */

#define CMSDK_ADC_EN_Pos			0 
#define CMSDK_ADC_EN_Msk			(0x01ul << CMSDK_ADC_EN_Pos)         		/*!< CMSDK_ADC_CTRL REG ADC enable: ADC enable Mask */

#define CMSDK_ADC_START_Pos			8 
#define CMSDK_ADC_START_Msk			(0x01ul << CMSDK_ADC_START_Pos)         	/*!< CMSDK_ADC_CTRL REG ADC start: ADC start Mask */

#define CMSDK_ADC_EOC_INT_EN_Pos		0
#define CMSDK_ADC_EOC_INT_EN_Msk		(0x01ul << CMSDK_ADC_EOC_INT_EN_Pos)         	/*!< CMSDK_ADC_INT_EN REG ADC End of Conversion Interrupt enable bit: ADC End of Conversion Interrupt enable bit Mask */

#define CMSDK_ADC_OVERRUN_INT_EN_Pos		1
#define CMSDK_ADC_OVERRUN_INT_EN_Msk		(0x01ul << CMSDK_ADC_EOC_INT_EN_Pos)         	/*!< CMSDK_ADC_INT_EN REG ADC Overrun interrupt enable bit: ADC Overrun interrupt enable bit Mask */

#define CMSDK_ADC_EOC_INT_STS_Pos		0
#define CMSDK_ADC_EOC_INT_STS_Msk		(0x01ul << CMSDK_ADC_EOC_INT_STS_Pos)         	/*!< CMSDK_ADC_INT_STS REG ADC_interrupt at the end of EOC: ADC_interrupt at the end of EOC Mask */

#define CMSDK_ADC_OVERRUN_INT_STS_Pos		1
#define CMSDK_ADC_OVERRUN_INT_STS_Msk		(0x01ul << CMSDK_ADC_EOC_INT_EN_Pos)         	/*!< CMSDK_ADC_INT_STS REG ADC overrun interrupt: ADC overrun interrupt Mask */

#define CMSDK_ADC_EOC_FLAG_Pos			0
#define CMSDK_ADC_EOC_FLAG_Msk			(0x01ul << CMSDK_ADC_EOC_FLAG_Pos)         	/*!< CMSDK_ADC_STS REG ADC EOC FLAG Status: ADC EOC FLAG Status Mask */

#define CMSDK_ADC_STATUS_Pos			1
#define CMSDK_ADC_STATUS_Msk			(0x01ul << CMSDK_ADC_EOC_FLAG_Pos)         	/*!< CMSDK_ADC_STS REG ADC Status(idle&busy): ADC Status Mask */

#define CMSDK_ADC_CLK_DIV_Pos			0
#define CMSDK_ADC_CLK_DIV_Msk			(0x07ul << CMSDK_ADC_EOC_FLAG_Pos)         	/*!< CMSDK_ADC_CLK_DIV REG ADC Clock Divider: ADC Clock Divider Mask */

#define CMSDK_ADC_SAMPLING_TIME_Pos		0
#define CMSDK_ADC_SAMPLING_TIME_Msk		(0x03ul << CMSDK_ADC_SAMPLING_TIME_Pos)        	/*!< CMSDK_ADC_CLK_SAMP_TIME REG ADC_SAMPLING TIME: ADC_SAMPLING TIME Mask */

#define CMSDK_ADC_DATA_Pos			0
#define CMSDK_ADC_DATA_Msk			(0x0FFFul << CMSDK_ADC_DATA_Pos)         	/*!< CMSDK_ADC_DATA REG ADC_SAMPLING TIME: ADC_ADC_DATA Mask */

#define CMSDK_ADC_CHANNEL_SEL_Pos		0
#define CMSDK_ADC_CHANNEL_SEL_Msk		(0x07ul << CMSDK_ADC_DATA_Pos)         		/*!< CMSDK_CHANNEL_SEL REG ADC_CHANNEL SEL: ADC_CHANNEL SEL Mask */

#define CMSDK_ADC_CONVERSION_INC_EOC_Pos	0
#define CMSDK_ADC_CONVERSION_INC_EOC_Msk	(0x01ul << CMSDK_ADC_CONVERSION_INC_EOC_Pos)         	/*!< CMSDK_ADC_EOC_CONFIG REG start next conversion with/without receiving EOC: ADC start next conversion with/without receiving EOC Mask */

/*@}*/ /* end of group CMSDK_ADC */

/*------------  Analog Control  (ANAC) --------------------------------------*/
/** @addtogroup CMSDK_ANAC
  @{
*/
typedef struct
{
  __IO  uint32_t  COMP0_CTRL;          	/*!< Offset: 0x000 Analog Comparator 0 Control Register (R/W) */
  __IO  uint32_t  COMP1_CTRL;       	/*!< Offset: 0x004 Analog Comparator 1 Control Register (R/W) */
  __IO  uint32_t  PGA_CTRL;       	/*!< Offset: 0x008 Analog PGA Control Register (R/W) */
  __IO  uint32_t  CHARGE_CTRL;		/*!< Offset: 0x00C Analog Charge Control Register (R) */ 
  __IO  uint32_t  PMU_CTRL;		/*!< Offset: 0x010 Analog PMU Control Register (R/W) */
  __IO  uint32_t  BOOST_CTRL;		/*!< Offset: 0x014 Analog Boost Control register (R/W) */
  __IO  uint32_t  ANA_BIST;		/*!< Offset: 0x018 Analog BIST Select Register (R/W) */ 
  } CMSDK_ANAC_TypeDef;

#define CMSDK_ANAC_BOOST_SELV_SEL_Pos		8
#define CMSDK_ANAC_BOOST_SELV_SEL_Msk		(0x0002ul << CMSDK_ANAC_BOOST_SELV_SEL_Pos)       /*!<  */	
	
#define CMSDK_ANAC_BOOST_PRES_Pos		16
#define CMSDK_ANAC_BOOST_PRES_Msk		(0x00ul << CMSDK_ANAC_BOOST_PRES_Pos)         	/*!< CMSDK_ANAC BOOST_CTRL REG : BOOST_PRES Mask */

#define CMSDK_ANAC_BOOST_DUTY_Pos		19
#define CMSDK_ANAC_BOOST_DUTY_Msk		(0x00ul << CMSDK_ANAC_BOOST_DUTY_Pos)       	/*!< CMSDK_ANAC BOOST_CTRL REG : BOOST_DUTY Mask */

#define CMSDK_ANAC_BOOST_DUTY_SEL_Pos		20
#define CMSDK_ANAC_BOOST_DUTY_SEL_Msk		(0x01ul << CMSDK_ANAC_BOOST_DUTY_SEL_Pos)       /*!< CMSDK_ANAC BOOST_CTRL REG : BOOST_DUTY_SEL Mask */

#define CMSDK_ANAC_BOOST_EXMODE_EN_Pos		2
#define CMSDK_ANAC_BOOST_EXMODE_EN_Msk		(0x01ul << CMSDK_ANAC_BOOST_EXMODE_EN_Pos)       /*!< CMSDK_ANAC BOOST_CTRL REG : BOOST_EXMODE_EN Mask */

#define CMSDK_ANAC_BOOST_STANDBY_EN_Pos		1
#define CMSDK_ANAC_BOOST_STANDBY_EN_Msk		(0x01ul << CMSDK_ANAC_BOOST_STANDBY_EN_Pos)       /*!< CMSDK_ANAC BOOST_CTRL REG : BOOST_STANDBY_EN Mask */

#define CMSDK_ANAC_BOOST_EN_Pos			0
#define CMSDK_ANAC_BOOST_EN_Msk			(0x01ul << CMSDK_ANAC_BOOST_EN_Pos)       	/*!< CMSDK_ANAC BOOST_CTRL REG : BOOST_EN Mask */
/*@}*/ /* end of group CMSDK_ANAC */

/*------------  WAVEFORM GENERATOR  (WAVE_GEN) --------------------------------------*/
/** @addtogroup CMSDK_WAVE_GEN
  @{
*/
typedef struct
{
  __IO uint32_t  WAVE_GEN_DRV_CONFIG_REG;          	
  __IO uint32_t  WAVE_GEN_DRV_CTRL_REG;       		
  __O  uint32_t  WAVE_GEN_DRV_REST_T_REG;       	
  __O  uint32_t  WAVE_GEN_DRV_SILENT_T_REG;   		
  __O  uint32_t  WAVE_GEN_DRV_HLF_WAVE_PRD_REG;   	
  __O  uint32_t  WAVE_GEN_DRV_NEG_HLF_WAVE_PRD_REG;	
  __O  uint32_t  WAVE_GEN_DRV_CLK_FREQ_REG;			
  __O  uint32_t  WAVE_GEN_DRV_IN_WAVE_ADDR_REG;		
  __O  uint32_t  WAVE_GEN_DRV_IN_WAVE_REG;			
  __O  uint32_t  WAVE_GEN_DRV_ALT_LIM_REG;			
  __O  uint32_t  WAVE_GEN_DRV_ALT_SILENT_LIM_REG;	
  __O  uint32_t  WAVE_GEN_DRV_DELAY_LIM_REG;
  __O  uint32_t  WAVE_GEN_DRV_NEG_SCALE_REG;
  __O  uint32_t  WAVE_GEN_DRV_NEG_OFFSET_REG;
  __IO uint32_t  WAVE_GEN_DRV_INT_REG;
  __O  uint32_t  WAVE_GEN_DRV_ISEL_REG;
  __O  uint32_t  WAVE_GEN_DRV_SW_CONFIG_REG;
  
  } CMSDK_WAVE_GEN_TypeDef;

#define CMSDK_WAVE_GEN_DRV_INT_EN_Pos			0
#define CMSDK_WAVE_GEN_DRV_INT_EN_Msk			(0x01ul << CMSDK_WAVE_GEN_DRV_INT_EN_Pos)         			/*!< WAVE_GEN_DRV_INT_REG Enable interrupt Mask */

#define CMSDK_WAVE_GEN_DRV_INT_FIRSTADDR_CLR_Pos	1
#define CMSDK_WAVE_GEN_DRV_INT_FIRSTADDR_CLR_Msk	(0x01ul << CMSDK_WAVE_GEN_DRV_INT_FIRSTADDR_CLR_Pos)         		/*!< WAVE_GEN_DRV_INT_REG Clear 1st ADDRESS interrupt Mask */

#define CMSDK_WAVE_GEN_DRV_INT_SECONDADDR_CLR_Pos	2
#define CMSDK_WAVE_GEN_DRV_INT_SECONDADDR_CLR_Msk	(0x01ul << CMSDK_WAVE_GEN_DRV_INT_SECONDADDR_CLR_Pos)         		/*!< WAVE_GEN_DRV_INT_REG Clear 2nd ADDRESS interrupt Mask */

#define CMSDK_WAVE_GEN_DRV_INT_FIRSTADDR_STS_Pos	9
#define CMSDK_WAVE_GEN_DRV_INT_FIRSTADDR_STS_Msk	(0x01ul << CMSDK_WAVE_GEN_DRV_INT_FIRSTADDR_STS_Pos)         		/*!< WAVE_GEN_DRV_INT_REG Status 1st ADDRESS interrupt Mask */

#define CMSDK_WAVE_GEN_DRV_INT_SECONDADDR_STS_Pos	10
#define CMSDK_WAVE_GEN_DRV_INT_SECONDADDR_STS_Msk	(0x01ul << CMSDK_WAVE_GEN_DRV_INT_SECONDADDR_STS_Pos)         		/*!< WAVE_GEN_DRV_INT_REG Status 2nd ADDRESS interrupt Mask */

#define CMSDK_WAVE_GEN_DRV_INT_FIRST_ADDR_Pos		8
#define CMSDK_WAVE_GEN_DRV_INT_FIRST_ADDR_Msk		(0xFFul << CMSDK_WAVE_GEN_DRV_INT_FIRST_ADDR_Pos)         		/*!< WAVE_GEN_DRV_INT_REG 1st ADDRESS Mask */

#define CMSDK_WAVE_GEN_DRV_INT_SECOND_ADDR_Pos		16
#define CMSDK_WAVE_GEN_DRV_INT_SECOND_ADDR_Msk		(0xFFul << CMSDK_WAVE_GEN_DRV_INT_SECOND_ADDR_Pos)         		/*!< WAVE_GEN_DRV_INT_REG 2nd ADDRESS Mask */

#define CMSDK_WAVE_GEN_DRV_INT_READ_FIRST_ADDR_Pos	16
#define CMSDK_WAVE_GEN_DRV_INT_READ_FIRST_ADDR_Msk	(0xFFul << CMSDK_WAVE_GEN_DRV_INT_READ_FIRST_ADDR_Pos)         		/*!< WAVE_GEN_DRV_INT_REG Reporting 1st ADDRESS Mask */

#define CMSDK_WAVE_GEN_DRV_INT_READ_SECOND_ADDR_Pos	24
#define CMSDK_WAVE_GEN_DRV_INT_READ_SECOND_ADDR_Msk	(0xFFul << CMSDK_WAVE_GEN_DRV_INT_READ_SECOND_ADDR_Pos)         	/*!< WAVE_GEN_DRV_INT_REG Reporting 2nd ADDRESS Mask */

#define CMSDK_WAVE_GEN_DRV_INT_READ_DRIVER_NUM_Pos	0
#define CMSDK_WAVE_GEN_DRV_INT_READ_DRIVER_NUM_Msk	(0xFFul << CMSDK_WAVE_GEN_DRV_INT_READ_DRIVER_NUM_Pos)         		/*!< WAVE_GEN_DRV_INT_REG Wavegen driver number Mask */
/*@}*/ /* end of group CMSDK_WAVE_GEN */

#if defined ( __CC_ARM   )
#pragma no_anon_unions
#endif

/*@}*/ /* end of group CMSDK_Peripherals */


/******************************************************************************/
/*                         Peripheral memory map                              */
/******************************************************************************/
/** @addtogroup CMSDK_MemoryMap CMSDK Memory Mapping
  @{
*/

/* Peripheral and SRAM base address */
#define CMSDK_CODE_BASE         (0x00000000UL)  /*!< (CODE     ) Base Address based on remap*/
#define CMSDK_MTP_BASE          (0x10000000UL)  /*!< (MTP     )  Base Address  */
#define CMSDK_SRAM_BASE         (0x20000000UL)  /*!< (SRAM      ) Base Address */
#define CMSDK_PERIPH_BASE       (0x40000000UL)  /*!< (Peripheral) Base Address */

#define CMSDK_RAM_BASE          (0x20000000UL)
#define CMSDK_APB_BASE          (0x40000000UL)
#define CMSDK_AHB_BASE          (0x40020000UL)

/* APB peripherals                                                           */
#define CMSDK_UART0_BASE        (CMSDK_APB_BASE + 0x0000UL)
#define CMSDK_UART1_BASE        (CMSDK_APB_BASE + 0x1000UL)
#define CMSDK_SPI0_BASE         (CMSDK_APB_BASE + 0x2000UL)
#define CMSDK_SPI1_BASE         (CMSDK_APB_BASE + 0x3000UL)
#define CMSDK_I2C0_BASE         (CMSDK_APB_BASE + 0x4000UL)
#define CMSDK_I2C1_BASE         (CMSDK_APB_BASE + 0x5000UL)
#define CMSDK_WDT_BASE          (CMSDK_APB_BASE + 0x6000UL)
#define CMSDK_PWM_BASE          (CMSDK_APB_BASE + 0x7000UL)
#define CMSDK_TIMER0_BASE       (CMSDK_APB_BASE + 0x8000UL)
#define CMSDK_TIMER1_BASE       (CMSDK_APB_BASE + 0x9000UL)
#define CMSDK_DUALTIMER_BASE    (CMSDK_APB_BASE + 0xA000UL)
#define CMSDK_DUALTIMER_1_BASE  (CMSDK_DUALTIMER_BASE)
#define CMSDK_DUALTIMER_2_BASE  (CMSDK_DUALTIMER_BASE + 0x20UL)
#define CMSDK_LCD_BASE          (CMSDK_APB_BASE + 0xB000UL)
#define CMSDK_WAVE_GEN_BASE     (CMSDK_APB_BASE + 0xC000UL)
#define CMSDK_ADC_BASE          (CMSDK_APB_BASE + 0xD000UL)
#define CMSDK_ANAC_BASE         (CMSDK_APB_BASE + 0xE000UL)
#define CMSDK_RTC_BASE          (CMSDK_APB_BASE + 0xF000UL)




/* AHB peripherals                                                           */
#define CMSDK_SYSCTRL_BASE      (CMSDK_AHB_BASE + 0x0000UL)
#define CMSDK_GPIO_BASE         (CMSDK_AHB_BASE + 0x1000UL)
#define CMSDK_EXTI_BASE         (CMSDK_AHB_BASE + 0x2000UL)
#define CMSDK_MTPREG_BASE       (CMSDK_AHB_BASE + 0x3000UL)
/*@}*/ /* end of group CMSDK_MemoryMap */


/******************************************************************************/
/*                         Peripheral declaration                             */
/******************************************************************************/

/** @addtogroup CMSDK_PeripheralDecl CMSDK Peripheral Declaration
  @{
*/

#define CMSDK_UART0             ((CMSDK_UART_TypeDef   *) CMSDK_UART0_BASE   )
#define CMSDK_UART1             ((CMSDK_UART_TypeDef   *) CMSDK_UART1_BASE   )
#define CMSDK_SPI0              ((CMSDK_SPI_TypeDef    *) CMSDK_SPI0_BASE    )
#define CMSDK_SPI1              ((CMSDK_SPI_TypeDef    *) CMSDK_SPI1_BASE    )
#define CMSDK_I2C0              ((CMSDK_I2C_TypeDef    *) CMSDK_I2C0_BASE    )
#define CMSDK_I2C1              ((CMSDK_I2C_TypeDef    *) CMSDK_I2C1_BASE    )
#define CMSDK_WATCHDOG          ((CMSDK_WDT_TypeDef    *) CMSDK_WDT_BASE     )
#define CMSDK_PWM               ((CMSDK_PWM_TypeDef    *) CMSDK_PWM_BASE     )
#define CMSDK_TIMER0            ((CMSDK_TIMER_TypeDef  *) CMSDK_TIMER0_BASE  )
#define CMSDK_TIMER1            ((CMSDK_TIMER_TypeDef  *) CMSDK_TIMER1_BASE  )
#define CMSDK_DUALTIMER1        ((CMSDK_DUALTIMER_TypeDef  *) CMSDK_DUALTIMER_1_BASE )
#define CMSDK_DUALTIMER2        ((CMSDK_DUALTIMER_TypeDef  *) CMSDK_DUALTIMER_2_BASE )
#define CMSDK_LCD               ((CMSDK_LCD_TypeDef    *) CMSDK_LCD_BASE     )
//#define CMSDK_WAVE_GEN          ((CMSDK_WAVE_GEN_TypeDef    *) CMSDK_WAVE_GEN_BASE  )
#define WAVE_GEN_DRVA_BLK0      ((CMSDK_WAVE_GEN_TypeDef    *) CMSDK_WAVE_GEN_BASE   )
#define WAVE_GEN_DRVA_BLK1      ((CMSDK_WAVE_GEN_TypeDef    *) 0x4000C044UL  )
#define WAVE_GEN_DRVA_BLK2      ((CMSDK_WAVE_GEN_TypeDef    *) 0x4000C088UL  )
#define WAVE_GEN_DRVA_BLK3      ((CMSDK_WAVE_GEN_TypeDef    *) 0x4000C0CCUL  )

#define WAVE_GEN_DRVB_BLK4      ((CMSDK_WAVE_GEN_TypeDef    *) 0x4000C110UL  )

#define WAVE_GEN_DRVC_BLK5      ((CMSDK_WAVE_GEN_TypeDef    *) 0x4000C154UL  )
#define WAVE_GEN_DRVC_BLK6      ((CMSDK_WAVE_GEN_TypeDef    *) 0x4000C198UL  )
#define WAVE_GEN_DRVC_BLK7      ((CMSDK_WAVE_GEN_TypeDef    *) 0x4000C1DCUL  )
#define WAVE_GEN_DRVC_BLK8      ((CMSDK_WAVE_GEN_TypeDef    *) 0x4000C220UL  )
#define WAVE_GEN_DRVC_BLK9      ((CMSDK_WAVE_GEN_TypeDef    *) 0x4000C264UL  )
#define WAVE_GEN_DRVC_BLK10     ((CMSDK_WAVE_GEN_TypeDef    *) 0x4000C2A8UL  )
#define WAVE_GEN_DRVC_BLK11     ((CMSDK_WAVE_GEN_TypeDef    *) 0x4000C2ECUL  )
#define WAVE_GEN_DRVC_BLK12     ((CMSDK_WAVE_GEN_TypeDef    *) 0x4000C330UL  )

#define CMSDK_ADC               ((CMSDK_ADC_TypeDef    *) CMSDK_ADC_BASE     )
#define CMSDK_ANAC              ((CMSDK_ANAC_TypeDef   *) CMSDK_ANAC_BASE    )
#define CMSDK_RTC               ((CMSDK_RTC_TypeDef    *) CMSDK_RTC_BASE     )

#define CMSDK_SYSCON            ((CMSDK_SYSCON_TypeDef *) CMSDK_SYSCTRL_BASE )
#define CMSDK_GPIO              ((CMSDK_GPIO_TypeDef   *) CMSDK_GPIO_BASE    )
#define CMSDK_EXTI              ((CMSDK_EXTI_TypeDef   *) CMSDK_EXTI_BASE    )
#define CMSDK_MTPREG            ((CMSDK_MTPREG_TypeDef   *) CMSDK_MTPREG_BASE    )
/*@}*/ /* end of group CMSDK_PeripheralDecl */

/*@}*/ /* end of group CMSDK_Definitions */

#ifdef __cplusplus
}
#endif

#endif  /* CMSDK_H */
