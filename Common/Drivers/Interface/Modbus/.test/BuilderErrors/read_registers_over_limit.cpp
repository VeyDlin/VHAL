// expect: FC 03 reads at most 125 registers
#include <Drivers/Interface/Modbus/Pdu/ReadHoldingRegisters.h>


void ReadTooMany() {
	ReadHoldingRegistersPdu::RequireCount<126>();
}
