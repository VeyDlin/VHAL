// expect: a coil write takes 1 to 1968 coils
#include <Drivers/Interface/Modbus/ModbusServer.h>


static_assert(ServerBuilder().MaxCoilWrite(2000).maxCoilWrite == 2000);
