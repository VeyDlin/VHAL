#pragma once
// Register model of the F4 SPI peripheral for the host tests. It has the CMSIS bit names and the LL functions that
// SPIAdapterF4.h and the shared SPI base reach, and nothing else. The transmitter is always empty (TXE stays set). Each data
// write clocks one byte in: the next byte of the receive queue, or 0xEE when the queue is empty. If the receive register is
// still full, the overrun flag is raised instead, as on the device. The test drives the interrupt handler by hand, so each
// call sees the flags the model has set.
#include <System/System.h>
#include <deque>
#include <vector>


// CR1 bits.
#define SPI_CR1_SPE      (1U << 6)

// CR2 interrupt enables.
#define SPI_CR2_ERRIE    (1U << 5)
#define SPI_CR2_RXNEIE   (1U << 6)
#define SPI_CR2_TXEIE    (1U << 7)

// SR status bits.
#define SPI_SR_RXNE      (1U << 0)
#define SPI_SR_TXE       (1U << 1)
#define SPI_SR_CRCERR    (1U << 4)
#define SPI_SR_MODF      (1U << 5)
#define SPI_SR_OVR       (1U << 6)
#define SPI_SR_BSY       (1U << 7)
#define SPI_SR_FRE       (1U << 8)


// LL constants. The values are distinct codes; the model does not decode them.
#define LL_SPI_BAUDRATEPRESCALER_DIV2     0x00000000U
#define LL_SPI_BAUDRATEPRESCALER_DIV4     0x00000008U
#define LL_SPI_BAUDRATEPRESCALER_DIV8     0x00000010U
#define LL_SPI_BAUDRATEPRESCALER_DIV16    0x00000018U
#define LL_SPI_BAUDRATEPRESCALER_DIV32    0x00000020U
#define LL_SPI_BAUDRATEPRESCALER_DIV64    0x00000028U
#define LL_SPI_BAUDRATEPRESCALER_DIV128   0x00000030U
#define LL_SPI_BAUDRATEPRESCALER_DIV256   0x00000038U
#define LL_SPI_CRCCALCULATION_DISABLE     0x00000000U
#define LL_SPI_DATAWIDTH_8BIT             0x00000000U
#define LL_SPI_FULL_DUPLEX                0x00000000U
#define LL_SPI_HALF_DUPLEX_RX             0x00000001U
#define LL_SPI_HALF_DUPLEX_TX             0x00000002U
#define LL_SPI_LSB_FIRST                  0x00000001U
#define LL_SPI_MSB_FIRST                  0x00000000U
#define LL_SPI_MODE_MASTER                0x00000004U
#define LL_SPI_MODE_SLAVE                 0x00000000U
#define LL_SPI_NSS_SOFT                   0x00000200U
#define LL_SPI_PHASE_1EDGE                0x00000000U
#define LL_SPI_PHASE_2EDGE                0x00000001U
#define LL_SPI_POLARITY_HIGH              0x00000002U
#define LL_SPI_POLARITY_LOW               0x00000000U
#define LL_SPI_PROTOCOL_MOTOROLA          0x00000000U


typedef enum {
	SUCCESS = 0,
	ERROR = 1
} ErrorStatus;


struct SPI_TypeDef {
	uint32 CR1 = 0;
	uint32 CR2 = 0;
	uint32 SR = 0;
	uint32 DR = 0;
};


struct LL_SPI_InitTypeDef {
	uint32 TransferDirection = 0;
	uint32 Mode = 0;
	uint32 DataWidth = 0;
	uint32 ClockPolarity = 0;
	uint32 ClockPhase = 0;
	uint32 NSS = 0;
	uint32 BaudRate = 0;
	uint32 BitOrder = 0;
	uint32 CRCCalculation = 0;
	uint32 CRCPoly = 0;
};


// Model state. The test resets it before each case.
inline std::vector<uint8> simSpiWritten;
inline std::deque<uint8> simSpiRxQueue;
inline uint8 simSpiRxData = 0;
inline uint32 simPrimask = 0;


inline void SimSpiResetModel() {
	simSpiWritten.clear();
	simSpiRxQueue.clear();
	simSpiRxData = 0;
	simPrimask = 0;
}


// The interrupt mask register of the CMSIS core, as the adapter uses it around the abort.
inline uint32 __get_PRIMASK() {
	return simPrimask;
}


inline void __set_PRIMASK(uint32 value) {
	simPrimask = value;
}


inline void __disable_irq() {
	simPrimask = 1;
}


inline void LL_SPI_ClearFlag_CRCERR(SPI_TypeDef *spi) { spi->SR &= ~SPI_SR_CRCERR; }
inline void LL_SPI_ClearFlag_FRE(SPI_TypeDef *spi) { spi->SR &= ~SPI_SR_FRE; }
inline void LL_SPI_ClearFlag_OVR(SPI_TypeDef *spi) { spi->SR &= ~SPI_SR_OVR; }

// The mode fault clears SPE, as the device does.
inline void LL_SPI_ClearFlag_MODF(SPI_TypeDef *spi) {
	spi->SR &= ~SPI_SR_MODF;
	spi->CR1 &= ~SPI_CR1_SPE;
}

inline void LL_SPI_Enable(SPI_TypeDef *spi) { spi->CR1 |= SPI_CR1_SPE; }
inline uint32 LL_SPI_IsEnabled(SPI_TypeDef *spi) { return (spi->CR1 & SPI_CR1_SPE) != 0; }

inline void LL_SPI_EnableIT_ERR(SPI_TypeDef *spi) { spi->CR2 |= SPI_CR2_ERRIE; }
inline void LL_SPI_EnableIT_RXNE(SPI_TypeDef *spi) { spi->CR2 |= SPI_CR2_RXNEIE; }
inline void LL_SPI_EnableIT_TXE(SPI_TypeDef *spi) { spi->CR2 |= SPI_CR2_TXEIE; }
inline void LL_SPI_DisableIT_ERR(SPI_TypeDef *spi) { spi->CR2 &= ~SPI_CR2_ERRIE; }
inline void LL_SPI_DisableIT_RXNE(SPI_TypeDef *spi) { spi->CR2 &= ~SPI_CR2_RXNEIE; }
inline void LL_SPI_DisableIT_TXE(SPI_TypeDef *spi) { spi->CR2 &= ~SPI_CR2_TXEIE; }
inline uint32 LL_SPI_IsEnabledIT_ERR(SPI_TypeDef *spi) { return (spi->CR2 & SPI_CR2_ERRIE) != 0; }
inline uint32 LL_SPI_IsEnabledIT_RXNE(SPI_TypeDef *spi) { return (spi->CR2 & SPI_CR2_RXNEIE) != 0; }
inline uint32 LL_SPI_IsEnabledIT_TXE(SPI_TypeDef *spi) { return (spi->CR2 & SPI_CR2_TXEIE) != 0; }

inline uint32 LL_SPI_IsActiveFlag_BSY(SPI_TypeDef *spi) { return (spi->SR & SPI_SR_BSY) != 0; }
inline uint32 LL_SPI_IsActiveFlag_CRCERR(SPI_TypeDef *spi) { return (spi->SR & SPI_SR_CRCERR) != 0; }
inline uint32 LL_SPI_IsActiveFlag_FRE(SPI_TypeDef *spi) { return (spi->SR & SPI_SR_FRE) != 0; }
inline uint32 LL_SPI_IsActiveFlag_MODF(SPI_TypeDef *spi) { return (spi->SR & SPI_SR_MODF) != 0; }
inline uint32 LL_SPI_IsActiveFlag_OVR(SPI_TypeDef *spi) { return (spi->SR & SPI_SR_OVR) != 0; }
inline uint32 LL_SPI_IsActiveFlag_RXNE(SPI_TypeDef *spi) { return (spi->SR & SPI_SR_RXNE) != 0; }
// The transmitter is always empty in the model: each write is accepted at once.
inline uint32 LL_SPI_IsActiveFlag_TXE(SPI_TypeDef *) { return 1; }

inline ErrorStatus LL_SPI_Init(SPI_TypeDef *, LL_SPI_InitTypeDef *) {
	return SUCCESS;
}

inline void LL_SPI_SetStandard(SPI_TypeDef *, uint32) { }


// A write clocks one byte in. The byte is lost to an overrun when the receive register is still full.
inline void LL_SPI_TransmitData8(SPI_TypeDef *spi, uint8 data) {
	simSpiWritten.push_back(data);

	uint8 received = 0xEE;
	if (!simSpiRxQueue.empty()) {
		received = simSpiRxQueue.front();
		simSpiRxQueue.pop_front();
	}

	if ((spi->SR & SPI_SR_RXNE) != 0) {
		spi->SR |= SPI_SR_OVR;
	} else {
		simSpiRxData = received;
		spi->SR |= SPI_SR_RXNE;
	}
}


// A read takes the byte and empties the receive register.
inline uint8 LL_SPI_ReceiveData8(SPI_TypeDef *spi) {
	spi->SR &= ~SPI_SR_RXNE;
	return simSpiRxData;
}
