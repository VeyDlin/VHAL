# Root of the VHAL test project. It adds the .test folder of every module under Common and Periphery, so that each module declares
# its own tests and the whole set is built and run as one CTest project. The vendor folders of the ports and the FreeRTOS kernel
# are not searched: they are large and hold no tests.

cmake_minimum_required(VERSION 3.25)

project(VhalTest LANGUAGES CXX)

enable_testing()

get_filename_component(VHAL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(VHAL_TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

set(VHAL_ARM_CXX "arm-none-eabi-g++" CACHE STRING "The ARM compiler of the size tests; GCC 14 or newer")
set(VHAL_ARM_SIZE "arm-none-eabi-size" CACHE STRING "The size tool that reads the ARM images")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(VhalTest.cmake)


# Finds the module folders under startDir: a folder that holds a .test folder with a CMakeLists.txt. The walk does not enter the
# .tool folders, the hidden folders or the vendor folders.
function(vhalFindModules startDir resultVar)
  set(queue "${startDir}")
  set(found "")

  list(LENGTH queue queueCount)
  while(queueCount GREATER 0)
    list(POP_FRONT queue dir)

    if(EXISTS "${dir}/.test/CMakeLists.txt")
      file(RELATIVE_PATH relativeDir "${VHAL_ROOT}" "${dir}")
      list(APPEND found "${relativeDir}")
    endif()

    file(GLOB children LIST_DIRECTORIES true "${dir}/*")
    foreach(child IN LISTS children)
      if(NOT IS_DIRECTORY "${child}")
        continue()
      endif()

      get_filename_component(childName "${child}" NAME)
      string(SUBSTRING "${childName}" 0 1 firstChar)
      if(firstChar STREQUAL ".")
        continue()
      endif()

      file(RELATIVE_PATH relativeChild "${VHAL_ROOT}" "${child}")
      if(relativeChild MATCHES "^Periphery/Adapter/Port/[^/]+/[^/]+/Drivers$")
        continue()
      endif()
      if(relativeChild STREQUAL "Periphery/OS/Port/FreeRTOS/Kernel")
        continue()
      endif()

      list(APPEND queue "${child}")
    endforeach()

    list(LENGTH queue queueCount)
  endwhile()

  set(${resultVar} "${found}" PARENT_SCOPE)
endfunction()


# The label of one module: its folder name, or the shortest path tail that no other module has the same tail of. The modules are
# relative paths from the repository root, all of them given in allModules.
function(vhalModuleLabel modulePath allModules resultVar)
  string(REPLACE "/" ";" moduleParts "${modulePath}")
  list(LENGTH moduleParts partCount)

  foreach(depth RANGE 1 ${partCount})
    math(EXPR tailStart "${partCount} - ${depth}")
    list(SUBLIST moduleParts ${tailStart} ${depth} tailParts)
    list(JOIN tailParts "/" tail)

    set(clash OFF)
    foreach(other IN LISTS allModules)
      if(other STREQUAL modulePath)
        continue()
      endif()

      string(REPLACE "/" ";" otherParts "${other}")
      list(LENGTH otherParts otherCount)
      if(otherCount LESS depth)
        continue()
      endif()

      math(EXPR otherStart "${otherCount} - ${depth}")
      list(SUBLIST otherParts ${otherStart} ${depth} otherTailParts)
      list(JOIN otherTailParts "/" otherTail)
      if(otherTail STREQUAL tail)
        set(clash ON)
      endif()
    endforeach()

    if(NOT clash)
      set(${resultVar} "${tail}" PARENT_SCOPE)
      return()
    endif()
  endforeach()

  set(${resultVar} "${modulePath}" PARENT_SCOPE)
endfunction()


vhalFindModules("${VHAL_ROOT}/Common" commonModules)
vhalFindModules("${VHAL_ROOT}/Periphery" peripheryModules)
set(moduleList ${commonModules} ${peripheryModules})
list(SORT moduleList)

foreach(relativeModule IN LISTS moduleList)
  vhalModuleLabel("${relativeModule}" "${moduleList}" VHAL_MODULE_LABEL)
  add_subdirectory("${VHAL_ROOT}/${relativeModule}/.test" "${CMAKE_BINARY_DIR}/modules/${relativeModule}")
endforeach()

# The frame's own self-test, under the label Frame. The .test folder is hidden, so the walk above does not find it.
set(VHAL_MODULE_LABEL "Frame")
add_subdirectory("${VHAL_TEST_DIR}/self" "${CMAKE_BINARY_DIR}/frame/self")
