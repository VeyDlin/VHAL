#pragma once
// Register model of the F4 USART for the host tests. It has the CMSIS bit names and the LL functions that UARTAdapterF4.h
// reaches, and nothing else. The test puts a received frame in with SimUsartReceive(), which raises RXNE as the hardware
// does. Reading the data register clears RXNE. The transmitter is always empty, so a write is accepted at once.
#include <System/System.h>
#include <cstdint>


// SR status bits.
#define USART_SR_PE      (1U << 0)
#define USART_SR_FE      (1U << 1)
#define USART_SR_NE      (1U << 2)
#define USART_SR_ORE     (1U << 3)
#define USART_SR_RXNE    (1U << 5)
#define USART_SR_TC      (1U << 6)
#define USART_SR_TXE     (1U << 7)

// CR1 bits.
#define USART_CR1_RE     (1U << 2)
#define USART_CR1_TE     (1U << 3)
#define USART_CR1_RXNEIE (1U << 5)
#define USART_CR1_TCIE   (1U << 6)
#define USART_CR1_TXEIE  (1U << 7)
#define USART_CR1_PEIE   (1U << 8)
#define USART_CR1_PS     (1U << 9)
#define USART_CR1_PCE    (1U << 10)
#define USART_CR1_M      (1U << 12)
#define USART_CR1_UE     (1U << 13)
#define USART_CR1_OVER8  (1U << 15)

// CR3 bit.
#define USART_CR3_EIE    (1U << 0)


// LL constants. The values are the register bits, as on the F4 device.
#define LL_USART_DIRECTION_TX        USART_CR1_TE
#define LL_USART_DIRECTION_RX        USART_CR1_RE
#define LL_USART_DIRECTION_TX_RX     (USART_CR1_TE | USART_CR1_RE)
#define LL_USART_DATAWIDTH_8B        0x00000000U
#define LL_USART_DATAWIDTH_9B        USART_CR1_M
#define LL_USART_PARITY_NONE         0x00000000U
#define LL_USART_PARITY_EVEN         USART_CR1_PCE
#define LL_USART_PARITY_ODD          (USART_CR1_PCE | USART_CR1_PS)
#define LL_USART_STOPBITS_1          0x00000000U
#define LL_USART_STOPBITS_2          0x00002000U
#define LL_USART_HWCONTROL_NONE      0x00000000U
#define LL_USART_HWCONTROL_RTS       0x00000100U
#define LL_USART_HWCONTROL_CTS       0x00000200U
#define LL_USART_HWCONTROL_RTS_CTS   0x00000300U
#define LL_USART_OVERSAMPLING_16     0x00000000U
#define LL_USART_OVERSAMPLING_8      USART_CR1_OVER8


typedef enum {
	SUCCESS = 0,
	ERROR = 1
} ErrorStatus;


typedef struct {
	uint32 SR;
	uint32 DR;
	uint32 BRR;
	uint32 CR1;
	uint32 CR2;
	uint32 CR3;
	uint32 GTPR;
} USART_TypeDef;


typedef struct {
	uint32 BaudRate;
	uint32 DataWidth;
	uint32 StopBits;
	uint32 Parity;
	uint32 TransferDirection;
	uint32 HardwareFlowControl;
	uint32 OverSampling;
} LL_USART_InitTypeDef;


// Test input: a frame as the receiver shifts it in, with the parity bit at the top of the word. Raises RXNE.
inline void SimUsartReceive(USART_TypeDef* usartx, uint32 frame) {
	usartx->DR = frame & 0x1FFU;
	usartx->SR |= USART_SR_RXNE;
}


// As in ST's LL driver, an enabled USART is not configured and the call returns ERROR.
inline ErrorStatus LL_USART_Init(USART_TypeDef* usartx, LL_USART_InitTypeDef* init) {
	if ((usartx->CR1 & USART_CR1_UE) != 0) {
		return ERROR;
	}

	usartx->CR1 = init->DataWidth | init->Parity | init->TransferDirection | init->OverSampling;
	usartx->CR2 = init->StopBits;
	usartx->CR3 = init->HardwareFlowControl;
	usartx->BRR = init->BaudRate;
	return SUCCESS;
}


inline void LL_USART_ConfigAsyncMode(USART_TypeDef*) { }


inline void LL_USART_Enable(USART_TypeDef* usartx) {
	usartx->CR1 |= USART_CR1_UE;
}


inline void LL_USART_Disable(USART_TypeDef* usartx) {
	usartx->CR1 &= ~USART_CR1_UE;
}


inline uint8 LL_USART_ReceiveData8(USART_TypeDef* usartx) {
	usartx->SR &= ~USART_SR_RXNE;
	return static_cast<uint8>(usartx->DR);
}


inline void LL_USART_TransmitData8(USART_TypeDef* usartx, uint8 value) {
	usartx->DR = value;
	usartx->SR |= USART_SR_TXE | USART_SR_TC;
}


inline bool LL_USART_IsActiveFlag_PE(USART_TypeDef* usartx) {
	return (usartx->SR & USART_SR_PE) != 0;
}


inline bool LL_USART_IsActiveFlag_FE(USART_TypeDef* usartx) {
	return (usartx->SR & USART_SR_FE) != 0;
}


inline bool LL_USART_IsActiveFlag_NE(USART_TypeDef* usartx) {
	return (usartx->SR & USART_SR_NE) != 0;
}


inline bool LL_USART_IsActiveFlag_ORE(USART_TypeDef* usartx) {
	return (usartx->SR & USART_SR_ORE) != 0;
}


inline bool LL_USART_IsActiveFlag_RXNE(USART_TypeDef* usartx) {
	return (usartx->SR & USART_SR_RXNE) != 0;
}


inline bool LL_USART_IsActiveFlag_TC(USART_TypeDef* usartx) {
	return (usartx->SR & USART_SR_TC) != 0;
}


inline bool LL_USART_IsActiveFlag_TXE(USART_TypeDef* usartx) {
	return (usartx->SR & USART_SR_TXE) != 0;
}


inline bool LL_USART_IsEnabledIT_ERROR(USART_TypeDef* usartx) {
	return (usartx->CR3 & USART_CR3_EIE) != 0;
}


inline bool LL_USART_IsEnabledIT_PE(USART_TypeDef* usartx) {
	return (usartx->CR1 & USART_CR1_PEIE) != 0;
}


inline bool LL_USART_IsEnabledIT_RXNE(USART_TypeDef* usartx) {
	return (usartx->CR1 & USART_CR1_RXNEIE) != 0;
}


inline bool LL_USART_IsEnabledIT_TC(USART_TypeDef* usartx) {
	return (usartx->CR1 & USART_CR1_TCIE) != 0;
}


inline bool LL_USART_IsEnabledIT_TXE(USART_TypeDef* usartx) {
	return (usartx->CR1 & USART_CR1_TXEIE) != 0;
}


inline void LL_USART_EnableIT_ERROR(USART_TypeDef* usartx) {
	usartx->CR3 |= USART_CR3_EIE;
}


inline void LL_USART_EnableIT_PE(USART_TypeDef* usartx) {
	usartx->CR1 |= USART_CR1_PEIE;
}


inline void LL_USART_EnableIT_RXNE(USART_TypeDef* usartx) {
	usartx->CR1 |= USART_CR1_RXNEIE;
}


inline void LL_USART_EnableIT_TC(USART_TypeDef* usartx) {
	usartx->CR1 |= USART_CR1_TCIE;
}


inline void LL_USART_EnableIT_TXE(USART_TypeDef* usartx) {
	usartx->CR1 |= USART_CR1_TXEIE;
}


inline void LL_USART_DisableIT_ERROR(USART_TypeDef* usartx) {
	usartx->CR3 &= ~USART_CR3_EIE;
}


inline void LL_USART_DisableIT_PE(USART_TypeDef* usartx) {
	usartx->CR1 &= ~USART_CR1_PEIE;
}


inline void LL_USART_DisableIT_RXNE(USART_TypeDef* usartx) {
	usartx->CR1 &= ~USART_CR1_RXNEIE;
}


inline void LL_USART_DisableIT_TC(USART_TypeDef* usartx) {
	usartx->CR1 &= ~USART_CR1_TCIE;
}


inline void LL_USART_DisableIT_TXE(USART_TypeDef* usartx) {
	usartx->CR1 &= ~USART_CR1_TXEIE;
}


inline void LL_USART_ClearFlag_PE(USART_TypeDef* usartx) {
	usartx->SR &= ~USART_SR_PE;
}


inline void LL_USART_ClearFlag_FE(USART_TypeDef* usartx) {
	usartx->SR &= ~USART_SR_FE;
}


inline void LL_USART_ClearFlag_NE(USART_TypeDef* usartx) {
	usartx->SR &= ~USART_SR_NE;
}


inline void LL_USART_ClearFlag_ORE(USART_TypeDef* usartx) {
	usartx->SR &= ~USART_SR_ORE;
}


inline void LL_USART_ClearFlag_TC(USART_TypeDef* usartx) {
	usartx->SR &= ~USART_SR_TC;
}
