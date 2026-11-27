# Host tests of the USB device stack on the software controller, the builder rule snippets, and the two counterpart tools. The include
# roots are the ones of the stack's test script, in its order: the shim of this folder, Periphery, Common, this folder, the USB common
# folder of the tools and the demo application folder.

set(usbTestIncludes
  "${CMAKE_CURRENT_SOURCE_DIR}/shim"
  "${VHAL_ROOT}/Periphery"
  "${VHAL_ROOT}/Common"
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${VHAL_ROOT}/Common/Drivers/Interface/USB/.tool/common"
  "${VHAL_ROOT}/.demo/STM32G4/Application")

vhal_test(GLOB Test*.cpp INCLUDES ${usbTestIncludes})

# The FAT12 test builds a volume of the largest size as a constant expression. It compiles with half of GCC's default operation
# limit, so a change that brings the builder near the default limit fails here first, on any compiler.
set_source_files_properties(TestFat12ImageBuilder.cpp PROPERTIES COMPILE_OPTIONS "-fconstexpr-ops-limit=16777216")

vhal_fail(BuilderErrors INCLUDES
  "${CMAKE_CURRENT_SOURCE_DIR}/shim"
  "${VHAL_ROOT}/Periphery"
  "${VHAL_ROOT}/Common")

vhal_tool_test(../.tool/usbbench)
vhal_tool_test(../.tool/usbclasscheck)
