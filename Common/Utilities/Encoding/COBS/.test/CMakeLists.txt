# Host tests of the COBS encoder and decoder, and the check that the Cobs.h header compiles alone.
# The sanitizers run the host tests when the root is configured with --sanitize (./test.cmd --sanitize COBS).

vhal_test(GLOB Test*.cpp INCLUDES "${VHAL_ROOT}/Common")

vhal_headers(
  "${VHAL_ROOT}/Common/Utilities/Encoding/COBS/Cobs.h"
  INCLUDES
    "${VHAL_ROOT}/Common")
