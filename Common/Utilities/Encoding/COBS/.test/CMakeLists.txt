# Host tests of the COBS encoder and decoder, and the check that the Cobs.h header compiles alone.

vhal_test(GLOB Test*.cpp INCLUDES "${VHAL_ROOT}/Common")

vhal_check(HeaderAlone.cmake)
