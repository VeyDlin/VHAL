// expect: timeout must be at most 4294967 ms
#include <Drivers/Interface/Modbus/ModbusClient.h>


static_assert(ClientBuilder().Timeout(4294968).timeoutMs == 4294968);
