;/**************************************************************************//**
; * @file     startup_CMSDK_CM0.s
; * @brief    CMSIS Cortex-M0 Core Device Startup File for
; *           Device CMSDK_CM0
; * @version  V3.01
; * @date     06. March 2012
; *
; * @note
; * Copyright (C) 2012 ARM Limited. All rights reserved.
; *
; * @par
; * ARM Limited (ARM) is supplying this software for use with Cortex-M
; * processor based microcontrollers.  This file can be freely distributed
; * within development tools that are supporting such ARM based processors.
; *
; * @par
; * THIS SOFTWARE IS PROVIDED "AS IS".  NO WARRANTIES, WHETHER EXPRESS, IMPLIED
; * OR STATUTORY, INCLUDING, BUT NOT LIMITED TO, IMPLIED WARRANTIES OF
; * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE APPLY TO THIS SOFTWARE.
; * ARM SHALL NOT, IN ANY CIRCUMSTANCES, BE LIABLE FOR SPECIAL, INCIDENTAL, OR
; * CONSEQUENTIAL DAMAGES, FOR ANY REASON WHATSOEVER.
; *
; ******************************************************************************/
;/*
;//-------- <<< Use Configuration Wizard in Context Menu >>> ------------------
;*/


; <h> Stack Configuration
;   <o> Stack Size (in Bytes) <0x0-0xFFFFFFFF:8>
; </h>

Stack_Size      EQU     0x00000200

                AREA    STACK, NOINIT, READWRITE, ALIGN=3
Stack_Mem       SPACE   Stack_Size
__initial_sp


; <h> Heap Configuration
;   <o>  Heap Size (in Bytes) <0x0-0xFFFFFFFF:8>
; </h>

Heap_Size       EQU     0x00000100

                AREA    HEAP, NOINIT, READWRITE, ALIGN=3
__heap_base
Heap_Mem        SPACE   Heap_Size
__heap_limit


                PRESERVE8
                THUMB


; Vector Table Mapped to Address 0 at Reset

                AREA    RESET, DATA, READONLY
                EXPORT  __Vectors
                EXPORT  __Vectors_End
                EXPORT  __Vectors_Size

__Vectors       DCD     __initial_sp              ; Top of Stack
                DCD     Reset_Handler             ; Reset Handler
                DCD     NMI_Handler               ; NMI Handler
                DCD     HardFault_Handler         ; Hard Fault Handler
                DCD     0                         ; Reserved
                DCD     0                         ; Reserved
                DCD     0                         ; Reserved
                DCD     0                         ; Reserved
                DCD     0                         ; Reserved
                DCD     0                         ; Reserved
                DCD     0                         ; Reserved
                DCD     SVC_Handler               ; SVCall Handler
                DCD     0                         ; Reserved
                DCD     0                         ; Reserved
                DCD     PendSV_Handler            ; PendSV Handler
                DCD     SysTick_Handler           ; SysTick Handler
                DCD     LVD_Handler               ; 
                DCD     RTC_Handler               ; 
                DCD     COMP0_Handler             ; 
                DCD     COMP1_Handler             ; 
                DCD     GPIO0_7_Handler           ; 
                DCD     GPIO8_15_Handler          ; 
                DCD     GPIO16_23_Handler         ; 
                DCD     MTP_Handler               ; 
                DCD     CHARGER_OK_Handler        ; 
                DCD     CHARGER_END_Handler       ; 
                DCD     ADC_Handler               ; 
                DCD     LCD_Handler               ; 
                DCD     UART0_Handler             ; 
                DCD     UART1_Handler             ; 
                DCD     SPI0_Handler              ; 
                DCD     SPI1_Handler              ; 
                DCD     I2C0_Event_Handler        ; 
                DCD     I2C0_Error_Handler        ; 
                DCD     I2C1_Event_Handler        ; 
                DCD     I2C1_Error_Handler        ; 
                DCD     PWM_Handler               ; 
                DCD     TIMER0_Handler            ; 
                DCD     TIMER1_Handler            ; 
                DCD     DUALTIMER_Handler         ; 
                DCD     OVER_TEMP_Handler         ; 
                DCD     WG_DRV_Handler                         ; 
                DCD     0                         ; 
                DCD     0                         ; 
                DCD     0                         ; 
                DCD     0                         ; 
                DCD     0                         ; 
                DCD     0                         ; 
__Vectors_End

__Vectors_Size  EQU     __Vectors_End - __Vectors

                AREA    |.text|, CODE, READONLY


; Reset Handler

Reset_Handler   PROC
                EXPORT  Reset_Handler             [WEAK]
                IMPORT  SystemInit
                IMPORT  __main
                LDR     R0, =SystemInit
                BLX     R0
                LDR     R0, =__main
                BX      R0
                ENDP


; Dummy Exception Handlers (infinite loops which can be modified)

NMI_Handler     PROC
                EXPORT  NMI_Handler               [WEAK]
                B       .
                ENDP
HardFault_Handler\
                PROC
                EXPORT  HardFault_Handler         [WEAK]
                B       .
                ENDP
SVC_Handler     PROC
                EXPORT  SVC_Handler               [WEAK]
                B       .
                ENDP
PendSV_Handler  PROC
                EXPORT  PendSV_Handler            [WEAK]
                B       .
                ENDP
SysTick_Handler PROC
               EXPORT  SysTick_Handler            [WEAK]
               B       .
               ENDP
Default_Handler PROC
                EXPORT LVD_Handler            [WEAK]
                EXPORT RTC_Handler            [WEAK]
                EXPORT COMP0_Handler          [WEAK]
                EXPORT COMP1_Handler          [WEAK]
                EXPORT GPIO0_7_Handler        [WEAK]
                EXPORT GPIO8_15_Handler       [WEAK]
                EXPORT GPIO16_23_Handler      [WEAK]
                EXPORT MTP_Handler            [WEAK]
                EXPORT CHARGER_OK_Handler     [WEAK]
                EXPORT CHARGER_END_Handler    [WEAK]
                EXPORT ADC_Handler            [WEAK]
                EXPORT LCD_Handler            [WEAK]
                EXPORT UART0_Handler          [WEAK]
                EXPORT UART1_Handler          [WEAK]
                EXPORT SPI0_Handler           [WEAK]
                EXPORT SPI1_Handler           [WEAK]
                EXPORT I2C0_Event_Handler     [WEAK]
                EXPORT I2C0_Error_Handler     [WEAK]
                EXPORT I2C1_Event_Handler     [WEAK]
                EXPORT I2C1_Error_Handler     [WEAK]
                EXPORT PWM_Handler            [WEAK]
                EXPORT TIMER0_Handler         [WEAK]
                EXPORT TIMER1_Handler         [WEAK]
                EXPORT DUALTIMER_Handler      [WEAK]
                EXPORT OVER_TEMP_Handler      [WEAK]
				EXPORT WG_DRV_Handler      	  [WEAK]
LVD_Handler
RTC_Handler
COMP0_Handler
COMP1_Handler
GPIO0_7_Handler
GPIO8_15_Handler
GPIO16_23_Handler
MTP_Handler
CHARGER_OK_Handler
CHARGER_END_Handler
ADC_Handler
LCD_Handler
UART0_Handler
UART1_Handler
SPI0_Handler
SPI1_Handler
I2C0_Event_Handler
I2C0_Error_Handler
I2C1_Event_Handler
I2C1_Error_Handler
PWM_Handler
TIMER0_Handler
TIMER1_Handler
DUALTIMER_Handler
OVER_TEMP_Handler
WG_DRV_Handler
                B       .
                ENDP


                ALIGN


; User Initial Stack & Heap

                IF      :DEF:__MICROLIB

                EXPORT  __initial_sp
                EXPORT  __heap_base
                EXPORT  __heap_limit

                ELSE

                IMPORT  __use_two_region_memory
                EXPORT  __user_initial_stackheap

__user_initial_stackheap PROC
                LDR     R0, =  Heap_Mem
                LDR     R1, =(Stack_Mem + Stack_Size)
                LDR     R2, = (Heap_Mem +  Heap_Size)
                LDR     R3, = Stack_Mem
                BX      LR
                ENDP

                ALIGN

                ENDIF


                END
