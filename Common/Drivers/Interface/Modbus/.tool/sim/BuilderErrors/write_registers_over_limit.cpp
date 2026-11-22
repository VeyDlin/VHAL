// expect: FC 10 writes at most 123 registers
#include <Drivers/Interface/Modbus/Pdu/WriteMultipleRegisters.h>


void WriteTooMany() {
	WriteMultipleRegistersPdu::RequireCount<124>();
}
