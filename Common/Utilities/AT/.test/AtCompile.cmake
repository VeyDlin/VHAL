# Helpers of the AT scripts: the include flags of the description's sources and a compile of one source to an object file. The
# scripts include this file. It defines variables and functions only. The inputs are VHAL_ROOT (the repository root).
cmake_minimum_required(VERSION 3.25)

set(atTestDir "${CMAKE_CURRENT_LIST_DIR}")

set(atIncludeFlags
  -I "${atTestDir}/shim"
  -I "${VHAL_ROOT}/Periphery"
  -I "${VHAL_ROOT}/Common")


# Compiles one source to one object file. The flags are the arguments after the compiler. Sets resultVar to "ok" or to "failed";
# the compiler's error output of a failure is printed.
function(atCompileObject resultVar compiler source object)
  execute_process(
    COMMAND "${compiler}" ${ARGN} -c -o "${object}" "${source}"
    RESULT_VARIABLE compileResult
    ERROR_VARIABLE compileErr
    OUTPUT_QUIET)

  if(compileResult EQUAL 0)
    set(${resultVar} "ok" PARENT_SCOPE)
  else()
    message("${compileErr}")
    set(${resultVar} "failed" PARENT_SCOPE)
  endif()
endfunction()
