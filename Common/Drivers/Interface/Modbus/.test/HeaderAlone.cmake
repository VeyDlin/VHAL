# Checks that each header of the Modbus layers compiles alone. Each header is the only include of a translation unit, which must pass
# -fsyntax-only with -std=c++23 -Wall -Wextra -Werror. The headers are the *.h files of the Modbus folder, the Pdu folder and three
# RTU headers. ModbusMaster.h and ModbusSlave.h in the RTU folder belong to the existing RTU code and are not checked here. The include
# roots are the shim, Periphery and Common. The inputs are VHAL_CXX (the compiler), VHAL_ROOT (the repository root) and VHAL_WORK_DIR
# (a folder the script may empty).
cmake_minimum_required(VERSION 3.25)

set(modbusDir "${VHAL_ROOT}/Common/Drivers/Interface/Modbus")

file(REMOVE_RECURSE "${VHAL_WORK_DIR}")
file(MAKE_DIRECTORY "${VHAL_WORK_DIR}")

file(GLOB modbusHeaders "${modbusDir}/*.h")
list(SORT modbusHeaders)
file(GLOB pduHeaders "${modbusDir}/Pdu/*.h")
list(SORT pduHeaders)

set(headers ${modbusHeaders} ${pduHeaders}
  "${modbusDir}/RTU/RtuFramer.h"
  "${modbusDir}/RTU/RtuTiming.h"
  "${modbusDir}/RTU/RtuTransport.h")

set(headerFailures 0)
foreach(header IN LISTS headers)
  file(RELATIVE_PATH relativeHeader "${VHAL_ROOT}" "${header}")
  set(probe "${VHAL_WORK_DIR}/HeaderProbe.cpp")
  file(WRITE "${probe}" "#include \"${header}\"\n")

  execute_process(
    COMMAND "${VHAL_CXX}" -std=c++23 -Wall -Wextra -Werror -fsyntax-only
      -I "${CMAKE_CURRENT_LIST_DIR}/shim"
      -I "${VHAL_ROOT}/Periphery"
      -I "${VHAL_ROOT}/Common"
      "${probe}"
    RESULT_VARIABLE compileResult
    OUTPUT_VARIABLE compileOut
    ERROR_VARIABLE compileErr)

  if(NOT compileResult EQUAL 0)
    message("${compileOut}${compileErr}")
    message("HEADER DOES NOT COMPILE ALONE: ${relativeHeader}")
    math(EXPR headerFailures "${headerFailures} + 1")
  endif()
endforeach()

if(headerFailures GREATER 0)
  message(FATAL_ERROR "${headerFailures} Modbus header(s) do not compile alone")
endif()

message("all Modbus headers compile alone")
