#pragma once
#include <System/System.h>
#include <Adapter/Utilities/IOption.h>
#include <initializer_list>
#include <functional>
#include <cassert>


class IAdapter {
public:
	std::function<ResultStatus()> afterPeripheryInit;
	std::function<ResultStatus()> beforePeripheryInit;

public:
	virtual ~IAdapter() = default;

protected:
	virtual ResultStatus Initialization() = 0;


	inline virtual ResultStatus AfterInitialization() {
		if(afterPeripheryInit != nullptr) {
			return afterPeripheryInit();
		}

		return ResultStatus::ok;
	}


	inline virtual ResultStatus BeforeInitialization() {
		if(beforePeripheryInit != nullptr) {
			return beforePeripheryInit();
		}

		return ResultStatus::ok;
	}
};
