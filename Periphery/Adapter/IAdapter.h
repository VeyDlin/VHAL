#pragma once
#include <System/System.h>
#include <functional>
#include <cassert>

#include "DMAAdapter.h"



class IAdapter {
public:
	std::function<Status::statusType()> afterPeripheryInit;
	std::function<Status::statusType()> beforePeripheryInit;



protected:
	virtual Status::statusType Initialization() = 0;
	DMAAdapter *dma;



	inline virtual Status::statusType AddDma(DMAAdapter *_dma) {
		dma = _dma;
		return Status::ok;
	}



	inline virtual Status::statusType AfterInitialization() {
		if(afterPeripheryInit != nullptr) {
			return afterPeripheryInit();
		}

		return Status::ok;
	}



	inline virtual Status::statusType BeforeInitialization() {
		if(beforePeripheryInit != nullptr) {
			return beforePeripheryInit();
		}

		return Status::ok;
	}
};
