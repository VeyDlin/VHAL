// expect: slave address must be 1 to 247 (0 is broadcast: use Broadcast())
#include <Drivers/Interface/Modbus/ModbusServer.h>


static_assert(ServerBuilder().SlaveAddress(248).slaveAddress == 248);
