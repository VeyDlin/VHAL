# Host tests of the callbacks, and the compile-time rules of the reference Ref, each snippet of BuilderErrors must fail to compile.

vhal_test(GLOB Test*.cpp INCLUDES "${VHAL_ROOT}/Common")

vhal_fail(BuilderErrors INCLUDES "${VHAL_ROOT}/Common")
