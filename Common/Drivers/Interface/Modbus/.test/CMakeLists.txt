# Host tests of the Modbus layers, the header checks, the builder error snippets and the size measurements of the probes. The shim
# replaces the umbrella header VHAL.h and comes first, then Periphery, then Common, then this folder.
# The sanitizers run the host tests when the root is configured with --sanitize (./test.cmd --sanitize Modbus).

vhal_test(
  GLOB Test*.cpp
  INCLUDES
    shim
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common"
    ".")

file(GLOB modbusHeaders "${VHAL_ROOT}/Common/Drivers/Interface/Modbus/*.h")
file(GLOB modbusPduHeaders "${VHAL_ROOT}/Common/Drivers/Interface/Modbus/Pdu/*.h")

vhal_headers(
  ${modbusHeaders}
  ${modbusPduHeaders}
  "${VHAL_ROOT}/Common/Drivers/Interface/Modbus/RTU/RtuFramer.h"
  "${VHAL_ROOT}/Common/Drivers/Interface/Modbus/RTU/RtuTiming.h"
  "${VHAL_ROOT}/Common/Drivers/Interface/Modbus/RTU/RtuTransport.h"
  INCLUDES
    shim
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common")

vhal_fail(
  BuilderErrors
  INCLUDES
    shim
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common")

vhal_size(size/size.cmake)
