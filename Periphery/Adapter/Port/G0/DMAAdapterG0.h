 #pragma once
#include "../../DMAAdapter.h"
#include <System/System.h>


using ADMA = class DMAAdapterG0;


class DMAAdapterG0: public DMAAdapter {

public:
	DMAAdapterG0() { }
	DMAAdapterG0(DMA_TypeDef *dma, uint32 channel, uint32 request):DMAAdapter(dma, channel, request) { }



	virtual inline void IrqHandler() override {

		// DMA half transfer
		if (IsActiveFlag_HT() && LL_DMA_IsEnabledIT_HT(dmaHandle, dmaChannel)) {
			if (LL_DMA_GetMode(dmaHandle, dmaChannel) == LL_DMA_MODE_NORMAL) {
				LL_DMA_DisableIT_HT(dmaHandle, dmaChannel);
			}

			ClearFlag_HT();
			//AdcDmaTransferHalf_Callback();
		}


		// DMA transfer complete
		if (IsActiveFlag_TC() && LL_DMA_IsEnabledIT_TC(dmaHandle, dmaChannel)) {
			if (LL_DMA_GetMode(dmaHandle, dmaChannel) == LL_DMA_MODE_NORMAL) {
				LL_DMA_DisableIT_HT(dmaHandle, dmaChannel);
				LL_DMA_DisableIT_TE(dmaHandle, dmaChannel);
			}

			ClearFlag_TC();
			//AdcDmaTransferComplete_Callback();
		}


		// DMA transfer error
		if (IsActiveFlag_TE() && LL_DMA_IsEnabledIT_TE(dmaHandle, dmaChannel)) {
			LL_DMA_DisableIT_TE(dmaHandle, dmaChannel);
			LL_DMA_DisableIT_TC(dmaHandle, dmaChannel);
			LL_DMA_DisableIT_HT(dmaHandle, dmaChannel);

			ClearFlag_HT();
			ClearFlag_TC();
			ClearFlag_TE();

			//AdcDmaTransferError_Callback();
		}
	}







protected:
	bool IsActiveFlag_HT() {
		return READ_BIT(dmaHandle->ISR, LL_DMA_ISR_HTIF1 << (GetChannelIndex() & 0x1C)) != 0;
	}


	bool IsActiveFlag_TC() {
		return READ_BIT(dmaHandle->ISR, LL_DMA_ISR_TCIF1 << (GetChannelIndex() & 0x1C)) != 0;
	}


	bool IsActiveFlag_TE() {
		return READ_BIT(dmaHandle->ISR, LL_DMA_ISR_TEIF1 << (GetChannelIndex() & 0x1C)) != 0;
	}


	void ClearFlag_HT() {
		SET_BIT(dmaHandle->IFCR, LL_DMA_ISR_HTIF1 << (GetChannelIndex() & 0x1C));
	}


	void ClearFlag_TC() {
		SET_BIT(dmaHandle->IFCR, LL_DMA_ISR_TCIF1 << (GetChannelIndex() & 0x1C));
	}


	void ClearFlag_TE() {
		SET_BIT(dmaHandle->IFCR, LL_DMA_ISR_TEIF1 << (GetChannelIndex() & 0x1C));
	}




	uint32 GetChannelIndex() {
		return (((uint32)CastChannel() - (uint32)DMA1_Channel1) / ((uint32)DMA1_Channel2 - (uint32)DMA1_Channel1)) << 2;
	}




	DMA_Channel_TypeDef* CastChannel() {
		switch (dmaChannel) {
			case 1: return DMA1_Channel1;
			case 2: return DMA1_Channel2;
			case 3: return DMA1_Channel3;
			case 4: return DMA1_Channel4;
			case 5: return DMA1_Channel5;
			case 6: return DMA1_Channel6;
			case 7: return DMA1_Channel7;
			default: SystemAbort();
		}
		return 0;
	}

};















