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

vhal_fail(BuilderErrors INCLUDES
  "${CMAKE_CURRENT_SOURCE_DIR}/shim"
  "${VHAL_ROOT}/Periphery"
  "${VHAL_ROOT}/Common")

vhal_tool_test(../.tool/usbbench)
vhal_tool_test(../.tool/usbclasscheck)
