#pragma once
// Register model of the F4 DMA controllers for the host tests. It has the stream registers, the LL functions and constants that
// DMAAdapterF4.h reaches, and nothing else. Each stream keeps its own SxCR, SxNDTR, SxPAR, SxM0AR and SxFCR, with the CMSIS bit
// positions of the control register, so a test can read the channel selection and the enable bit the adapter wrote. The model
// does not move data: a test sets the enable bit and reads back what the adapter programmed.
#include <System/System.h>


// SxCR bits.
#define DMA_SxCR_EN          (1U << 0)
#define DMA_SxCR_DMEIE       (1U << 1)
#define DMA_SxCR_TEIE        (1U << 2)
#define DMA_SxCR_HTIE        (1U << 3)
#define DMA_SxCR_TCIE        (1U << 4)
#define DMA_SxCR_PFCTRL      (1U << 5)
#define DMA_SxCR_DIR         (3U << 6)
#define DMA_SxCR_CIRC        (1U << 8)
#define DMA_SxCR_PINC        (1U << 9)
#define DMA_SxCR_MINC        (1U << 10)
#define DMA_SxCR_PSIZE       (3U << 11)
#define DMA_SxCR_MSIZE       (3U << 13)
#define DMA_SxCR_PL          (3U << 16)
#define DMA_SxCR_PBURST      (3U << 21)
#define DMA_SxCR_MBURST      (3U << 23)
#define DMA_SxCR_CHSEL       (7U << 25)

// SxFCR bits.
#define DMA_SxFCR_FTH        (3U << 0)
#define DMA_SxFCR_DMDIS      (1U << 2)
#define DMA_SxFCR_FEIE       (1U << 7)


// LL constants, with the values of the F4 LL header.
#define LL_DMA_CHANNEL_0                  0x00000000U
#define LL_DMA_CHANNEL_1                  (1U << 25)
#define LL_DMA_CHANNEL_2                  (2U << 25)
#define LL_DMA_CHANNEL_3                  (3U << 25)
#define LL_DMA_CHANNEL_4                  (4U << 25)
#define LL_DMA_CHANNEL_5                  (5U << 25)
#define LL_DMA_CHANNEL_6                  (6U << 25)
#define LL_DMA_CHANNEL_7                  (7U << 25)
#define LL_DMA_DIRECTION_PERIPH_TO_MEMORY 0x00000000U
#define LL_DMA_DIRECTION_MEMORY_TO_PERIPH (1U << 6)
#define LL_DMA_DIRECTION_MEMORY_TO_MEMORY (2U << 6)
#define LL_DMA_MODE_NORMAL                0x00000000U
#define LL_DMA_MODE_CIRCULAR              (1U << 8)
#define LL_DMA_PERIPH_NOINCREMENT         0x00000000U
#define LL_DMA_PERIPH_INCREMENT           (1U << 9)
#define LL_DMA_MEMORY_NOINCREMENT         0x00000000U
#define LL_DMA_MEMORY_INCREMENT           (1U << 10)
#define LL_DMA_PDATAALIGN_BYTE            0x00000000U
#define LL_DMA_PDATAALIGN_HALFWORD        (1U << 11)
#define LL_DMA_PDATAALIGN_WORD            (2U << 11)
#define LL_DMA_MDATAALIGN_BYTE            0x00000000U
#define LL_DMA_MDATAALIGN_HALFWORD        (1U << 13)
#define LL_DMA_MDATAALIGN_WORD            (2U << 13)
#define LL_DMA_PRIORITY_LOW               0x00000000U
#define LL_DMA_PRIORITY_MEDIUM            (1U << 16)
#define LL_DMA_PRIORITY_HIGH              (2U << 16)
#define LL_DMA_PRIORITY_VERYHIGH          (3U << 16)
#define LL_DMA_MBURST_SINGLE              0x00000000U
#define LL_DMA_MBURST_INC4                (1U << 23)
#define LL_DMA_MBURST_INC8                (2U << 23)
#define LL_DMA_MBURST_INC16               (3U << 23)
#define LL_DMA_PBURST_SINGLE              0x00000000U
#define LL_DMA_PBURST_INC4                (1U << 21)
#define LL_DMA_PBURST_INC8                (2U << 21)
#define LL_DMA_PBURST_INC16               (3U << 21)
#define LL_DMA_FIFOTHRESHOLD_1_4          0x00000000U
#define LL_DMA_FIFOTHRESHOLD_1_2          0x00000001U
#define LL_DMA_FIFOTHRESHOLD_3_4          0x00000002U
#define LL_DMA_FIFOTHRESHOLD_FULL         0x00000003U

// The RCC bit of each DMA controller in AHB1ENR. The model reports the clock as on.
#define LL_AHB1_GRP1_PERIPH_DMA1          (1U << 21)
#define LL_AHB1_GRP1_PERIPH_DMA2          (1U << 22)


// One controller: the flag registers and, for each of the eight streams, its configuration registers.
struct DMA_TypeDef {
	uint32 LISR = 0;
	uint32 HISR = 0;
	uint32 LIFCR = 0;
	uint32 HIFCR = 0;
	uint32 CR[8] = { };
	uint32 NDTR[8] = { };
	uint32 PAR[8] = { };
	uint32 M0AR[8] = { };
	uint32 FCR[8] = { };
};


// The two controllers. The adapter compares its handle with these, so the test uses them by address.
inline DMA_TypeDef simDma1;
inline DMA_TypeDef simDma2;

#define DMA1 (&simDma1)
#define DMA2 (&simDma2)


inline bool SimDmaStreamEnabled(const DMA_TypeDef *dma, uint32 stream) {
	return (dma->CR[stream] & DMA_SxCR_EN) != 0;
}


inline void SimDmaSetCr(DMA_TypeDef *dma, uint32 stream, uint32 mask, uint32 value) {
	dma->CR[stream] = (dma->CR[stream] & ~mask) | (value & mask);
}


inline void SimDmaSetFcr(DMA_TypeDef *dma, uint32 stream, uint32 mask, uint32 value) {
	dma->FCR[stream] = (dma->FCR[stream] & ~mask) | (value & mask);
}


inline void SimDmaSetIt(DMA_TypeDef *dma, uint32 stream, uint32 bit, bool enable) {
	if (enable) {
		dma->CR[stream] |= bit;
	} else {
		dma->CR[stream] &= ~bit;
	}
}


inline uint32 LL_AHB1_GRP1_IsEnabledClock(uint32) {
	return 1;
}


inline uint32 LL_DMA_IsEnabledStream(DMA_TypeDef *dma, uint32 stream) {
	return SimDmaStreamEnabled(dma, stream) ? 1U : 0U;
}


inline void LL_DMA_EnableStream(DMA_TypeDef *dma, uint32 stream) {
	dma->CR[stream] |= DMA_SxCR_EN;
}


inline void LL_DMA_DisableStream(DMA_TypeDef *dma, uint32 stream) {
	dma->CR[stream] &= ~DMA_SxCR_EN;
}


inline void LL_DMA_SetChannelSelection(DMA_TypeDef *dma, uint32 stream, uint32 channel) {
	SimDmaSetCr(dma, stream, DMA_SxCR_CHSEL, channel);
}


inline void LL_DMA_SetDataTransferDirection(DMA_TypeDef *dma, uint32 stream, uint32 direction) {
	SimDmaSetCr(dma, stream, DMA_SxCR_DIR, direction);
}


inline void LL_DMA_SetStreamPriorityLevel(DMA_TypeDef *dma, uint32 stream, uint32 priority) {
	SimDmaSetCr(dma, stream, DMA_SxCR_PL, priority);
}


inline void LL_DMA_SetMode(DMA_TypeDef *dma, uint32 stream, uint32 mode) {
	SimDmaSetCr(dma, stream, DMA_SxCR_CIRC | DMA_SxCR_PFCTRL, mode);
}


inline uint32 LL_DMA_GetMode(DMA_TypeDef *dma, uint32 stream) {
	return dma->CR[stream] & (DMA_SxCR_CIRC | DMA_SxCR_PFCTRL);
}


inline void LL_DMA_SetPeriphIncMode(DMA_TypeDef *dma, uint32 stream, uint32 mode) {
	SimDmaSetCr(dma, stream, DMA_SxCR_PINC, mode);
}


inline void LL_DMA_SetMemoryIncMode(DMA_TypeDef *dma, uint32 stream, uint32 mode) {
	SimDmaSetCr(dma, stream, DMA_SxCR_MINC, mode);
}


inline void LL_DMA_SetPeriphSize(DMA_TypeDef *dma, uint32 stream, uint32 size) {
	SimDmaSetCr(dma, stream, DMA_SxCR_PSIZE, size);
}


inline void LL_DMA_SetMemorySize(DMA_TypeDef *dma, uint32 stream, uint32 size) {
	SimDmaSetCr(dma, stream, DMA_SxCR_MSIZE, size);
}


inline void LL_DMA_SetMemoryBurstxfer(DMA_TypeDef *dma, uint32 stream, uint32 burst) {
	SimDmaSetCr(dma, stream, DMA_SxCR_MBURST, burst);
}


inline void LL_DMA_SetPeriphBurstxfer(DMA_TypeDef *dma, uint32 stream, uint32 burst) {
	SimDmaSetCr(dma, stream, DMA_SxCR_PBURST, burst);
}


inline void LL_DMA_EnableFifoMode(DMA_TypeDef *dma, uint32 stream) {
	SimDmaSetFcr(dma, stream, DMA_SxFCR_DMDIS, 0);
}


inline void LL_DMA_DisableFifoMode(DMA_TypeDef *dma, uint32 stream) {
	SimDmaSetFcr(dma, stream, DMA_SxFCR_DMDIS, DMA_SxFCR_DMDIS);
}


inline void LL_DMA_SetFIFOThreshold(DMA_TypeDef *dma, uint32 stream, uint32 threshold) {
	SimDmaSetFcr(dma, stream, DMA_SxFCR_FTH, threshold);
}


inline void LL_DMA_SetMemoryAddress(DMA_TypeDef *dma, uint32 stream, uint32 address) {
	dma->M0AR[stream] = address;
}


inline void LL_DMA_SetPeriphAddress(DMA_TypeDef *dma, uint32 stream, uint32 address) {
	dma->PAR[stream] = address;
}


inline void LL_DMA_SetDataLength(DMA_TypeDef *dma, uint32 stream, uint32 length) {
	dma->NDTR[stream] = length;
}


inline void LL_DMA_EnableIT_TE(DMA_TypeDef *dma, uint32 stream) { SimDmaSetIt(dma, stream, DMA_SxCR_TEIE, true); }
inline void LL_DMA_DisableIT_TE(DMA_TypeDef *dma, uint32 stream) { SimDmaSetIt(dma, stream, DMA_SxCR_TEIE, false); }
inline void LL_DMA_EnableIT_DME(DMA_TypeDef *dma, uint32 stream) { SimDmaSetIt(dma, stream, DMA_SxCR_DMEIE, true); }
inline void LL_DMA_DisableIT_DME(DMA_TypeDef *dma, uint32 stream) { SimDmaSetIt(dma, stream, DMA_SxCR_DMEIE, false); }
inline void LL_DMA_EnableIT_HT(DMA_TypeDef *dma, uint32 stream) { SimDmaSetIt(dma, stream, DMA_SxCR_HTIE, true); }
inline void LL_DMA_DisableIT_HT(DMA_TypeDef *dma, uint32 stream) { SimDmaSetIt(dma, stream, DMA_SxCR_HTIE, false); }
inline void LL_DMA_EnableIT_TC(DMA_TypeDef *dma, uint32 stream) { SimDmaSetIt(dma, stream, DMA_SxCR_TCIE, true); }
inline void LL_DMA_DisableIT_TC(DMA_TypeDef *dma, uint32 stream) { SimDmaSetIt(dma, stream, DMA_SxCR_TCIE, false); }
inline void LL_DMA_EnableIT_FE(DMA_TypeDef *dma, uint32 stream) { SimDmaSetFcr(dma, stream, DMA_SxFCR_FEIE, DMA_SxFCR_FEIE); }
inline void LL_DMA_DisableIT_FE(DMA_TypeDef *dma, uint32 stream) { SimDmaSetFcr(dma, stream, DMA_SxFCR_FEIE, 0); }

inline uint32 LL_DMA_IsEnabledIT_TE(DMA_TypeDef *dma, uint32 stream) { return (dma->CR[stream] & DMA_SxCR_TEIE) != 0; }
inline uint32 LL_DMA_IsEnabledIT_DME(DMA_TypeDef *dma, uint32 stream) { return (dma->CR[stream] & DMA_SxCR_DMEIE) != 0; }
inline uint32 LL_DMA_IsEnabledIT_HT(DMA_TypeDef *dma, uint32 stream) { return (dma->CR[stream] & DMA_SxCR_HTIE) != 0; }
inline uint32 LL_DMA_IsEnabledIT_TC(DMA_TypeDef *dma, uint32 stream) { return (dma->CR[stream] & DMA_SxCR_TCIE) != 0; }
inline uint32 LL_DMA_IsEnabledIT_FE(DMA_TypeDef *dma, uint32 stream) { return (dma->FCR[stream] & DMA_SxFCR_FEIE) != 0; }
