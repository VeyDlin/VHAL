# Host tests of the CRC library, the builder error snippets of its parameters and engines, and the size measurements of the engines
# and of the presets.

vhal_test(GLOB Test*.cpp INCLUDES "${VHAL_ROOT}/Common")

vhal_fail(BuilderErrors INCLUDES "${VHAL_ROOT}/Common")

vhal_size(size/Size.cmake)
vhal_size(size/Presets.cmake)
