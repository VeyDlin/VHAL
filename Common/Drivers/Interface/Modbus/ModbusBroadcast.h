#pragma once
#include <Drivers/Interface/Modbus/ModbusResult.h>
#include <Drivers/Interface/Modbus/Pdu/WriteMultipleCoils.h>
#include <Drivers/Interface/Modbus/Pdu/WriteMultipleRegisters.h>
#include <Drivers/Interface/Modbus/Pdu/WriteSingleCoil.h>
#include <Drivers/Interface/Modbus/Pdu/WriteSingleRegister.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <cstddef>
#include <span>


// The write functions of a client, sent to slave address 0. A broadcast gets no reply: each function returns ok once the frame has
// left the line and the turnaround has passed. There is no broadcast read, because a read has no meaning for every slave at once.
// The client calls SendBroadcast, which is private to it; this class is its friend.
//
//   ModbusBroadcast<Plc> all = plc.Broadcast();
//   all.WriteSingleRegister(0x0000, 1234);
template<typename Client>
class ModbusBroadcast {
private:
	Client* owner;


public:
	explicit ModbusBroadcast(Client& client) : owner(&client) {
	}


	ModbusResult WriteSingleCoil(uint16 address, bool on) {
		return owner->SendBroadcast(WriteSingleCoilPdu::RequestSize, [&](std::span<uint8> pdu) -> ResultStatus {
			return WriteSingleCoilPdu::BuildRequest(pdu, address, on);
		});
	}


	ModbusResult WriteSingleRegister(uint16 address, uint16 value) {
		return owner->SendBroadcast(WriteSingleRegisterPdu::RequestSize, [&](std::span<uint8> pdu) -> ResultStatus {
			return WriteSingleRegisterPdu::BuildRequest(pdu, address, value);
		});
	}


	ModbusResult WriteMultipleCoils(uint16 address, std::span<const bool> values) {
		if (values.empty() || values.size() > WriteMultipleCoilsPdu::MaxQuantity) {
			return ModbusResult{ResultStatus::invalidParameter, 0};
		}

		return owner->SendBroadcast(WriteMultipleCoilsPdu::RequestSize(values.size()), [&](std::span<uint8> pdu) -> ResultStatus {
			return WriteMultipleCoilsPdu::BuildRequest(pdu, address, values);
		});
	}


	ModbusResult WriteMultipleRegisters(uint16 address, std::span<const uint16> values) {
		if (values.empty() || values.size() > WriteMultipleRegistersPdu::MaxQuantity) {
			return ModbusResult{ResultStatus::invalidParameter, 0};
		}

		return owner->SendBroadcast(WriteMultipleRegistersPdu::RequestSize(values.size()), [&](std::span<uint8> pdu) -> ResultStatus {
			return WriteMultipleRegistersPdu::BuildRequest(pdu, address, values);
		});
	}
};
