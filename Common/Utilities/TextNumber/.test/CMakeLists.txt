# Host tests of the text number library: the integer and float parsers, the formats and the JSON golden text.

vhal_test(
  GLOB Test*.cpp
  INCLUDES
    shim
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common")
