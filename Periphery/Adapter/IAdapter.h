#pragma once
#include <System/System.h>
#include <functional>
#include <cassert>



class IAdapter {
public:
	std::function<Status::statusType()> afterPeripheryInit;
	std::function<Status::statusType()> beforePeripheryInit;

public:
	virtual ~IAdapter() = default;

protected:
	virtual Status::statusType Initialization() = 0;


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
