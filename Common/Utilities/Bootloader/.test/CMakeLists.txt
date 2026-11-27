# The unit tests of the bootloader run against the unit shim, which comes first: it replaces VHAL.h with a simulated clock and an
# in-memory flash. Periphery supplies System/System.h. The AES source is linked in: the bootloader's encrypted modes call it.
#
# The device simulator of the bootloader CLI is the stand-in that the CLI's end-to-end tests run. The CLI's own test suite runs with
# npm test in the .tool folder, after npm run sim:build has built the simulator (.test/build) with g++.

vhal_test(
  unit/TestAesBootloader.cpp
  INCLUDES
    unit/shim
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common"
  SOURCES
    ../../Crypto/AES/AES.cpp)

vhal_test(
  unit/TestBootloaderFlash.cpp
  INCLUDES
    unit/shim
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common"
  SOURCES
    ../../Crypto/AES/AES.cpp)

vhal_test(
  unit/TestBootloaderProtocol.cpp
  INCLUDES
    unit/shim
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common"
  SOURCES
    ../../Crypto/AES/AES.cpp)

vhal_declared(unit/Test*.cpp unit/TestAesBootloader.cpp unit/TestBootloaderFlash.cpp unit/TestBootloaderProtocol.cpp)

vhal_tool_test(../.tool PREPARE sim:build)
