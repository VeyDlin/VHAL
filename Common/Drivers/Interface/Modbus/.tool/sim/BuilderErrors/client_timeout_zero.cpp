// expect: timeout must be above 0 ms
#include <Drivers/Interface/Modbus/ModbusClient.h>


static_assert(ClientBuilder().Timeout(0).timeoutMs == 0);
