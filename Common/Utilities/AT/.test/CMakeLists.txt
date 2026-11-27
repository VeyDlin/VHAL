# Host tests of the AT description, the value grammar, the client, the server, the transcripts and the console arguments; the builder
# error snippets of the description; the compile checks of the scale set and the dialect set; and the size measurements of the
# dialect, of the ESP-AT client and of the five-command server and client.

vhal_test(
  GLOB Test*.cpp
  INCLUDES
    shim
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common"
    "${CMAKE_CURRENT_SOURCE_DIR}"
  OPTIONS -pthread
  LIBS -pthread)

vhal_fail(
  BuilderErrors
  INCLUDES
    shim
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common")

vhal_check(CheckHost.cmake)
vhal_check(CheckTarget.cmake)

vhal_size(size/Dialect.cmake)
vhal_size(size/EspAt.cmake)
vhal_size(size/ServerClient.cmake)
