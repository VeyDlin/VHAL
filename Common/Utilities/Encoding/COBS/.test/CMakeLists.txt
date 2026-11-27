# Host tests of the COBS encoder and decoder, and the check that the Cobs.h header compiles alone.
# The default run has no sanitizers. For a sanitizer run, delete .test-build and configure it again with CXXFLAGS set, for example
# CXXFLAGS="-fsanitize=address,undefined" ./test.cmd COBS. The flags stay in .test-build until it is deleted, and the compiler must
# have the sanitizer runtime (the MinGW toolchain on Windows does not, so configure fails there).

vhal_test(GLOB Test*.cpp INCLUDES "${VHAL_ROOT}/Common")

vhal_check(HeaderAlone.cmake)
