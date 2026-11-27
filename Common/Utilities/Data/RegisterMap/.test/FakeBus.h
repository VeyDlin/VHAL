#pragma once
#include <Utilities/DataTypes.h>
#include <Utilities/Data/RegisterMap/RegisterBus.h>
#include <Utilities/Result/ResultStatus.h>
#include <cstddef>
#include <span>
#include <vector>


// A fake register bus: one memory for all devices, indexed by register address, and a log of every transaction in order.
// failWith, when not ok, is returned by every transaction and no byte moves.


struct FakeBusTransaction {
	bool isRead;
	RegisterRef where;
	std::vector<uint8> bytes;
};


struct FakeRegisterBus {
	std::vector<uint8> memory = std::vector<uint8>(0x10100, 0);
	std::vector<FakeBusTransaction> log;
	ResultStatus failWith = ResultStatus::ok;


	ResultStatus ReadRegs(RegisterRef where, std::span<uint8> in) {
		FakeBusTransaction transaction{ true, where, std::vector<uint8>() };
		if (failWith != ResultStatus::ok) {
			log.push_back(transaction);
			return failWith;
		}

		for (std::size_t index = 0; index < in.size(); index++) {
			in[index] = memory[static_cast<std::size_t>(where.address) + index];
			transaction.bytes.push_back(in[index]);
		}
		log.push_back(transaction);
		return ResultStatus::ok;
	}


	ResultStatus WriteRegs(RegisterRef where, std::span<const uint8> out) {
		FakeBusTransaction transaction{ false, where, std::vector<uint8>() };
		if (failWith != ResultStatus::ok) {
			log.push_back(transaction);
			return failWith;
		}

		for (std::size_t index = 0; index < out.size(); index++) {
			memory[static_cast<std::size_t>(where.address) + index] = out[index];
			transaction.bytes.push_back(out[index]);
		}
		log.push_back(transaction);
		return ResultStatus::ok;
	}
};
