#pragma once
#include "IAdapter.h"

#define AUSED_DMA_ADAPTER



class DMAAdapter {
public:
	struct Parameters {

	};


protected:
	DMA_TypeDef *dmaHandle;
	uint32 dmaChannel;
	uint32 dmaRequest;

	Parameters parameters;



public:
	DMAAdapter() { }
	DMAAdapter(DMA_TypeDef *dma, uint32 channel, uint32 request):dmaHandle(dma), dmaChannel(channel), dmaRequest(request) { }




	virtual inline void IrqHandler() = 0;


};















