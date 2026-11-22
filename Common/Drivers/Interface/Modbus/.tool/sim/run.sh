#!/usr/bin/env bash
# Builds every host test of the Modbus layers and runs it with g++, checks that every header compiles alone, and checks the builder
# error snippets. Exits non-zero when a test fails to build or fails. Works in Git Bash on Windows and on Linux or macOS. Build output
# goes to a temporary directory outside the repository.
#
# CXXFLAGS adds flags to the test builds, for example CXXFLAGS="-fsanitize=address,undefined" where the toolchain has the sanitizer
# libraries. The default run does not use sanitizers.
set -u

simDir="$(cd "$(dirname "$0")" && pwd)"
vhalRoot="$(cd "$simDir/../../../../../.." && pwd)"
modbusDir="$vhalRoot/Common/Drivers/Interface/Modbus"
cxx="${CXX:-g++}"
buildDir="$(mktemp -d)"
trap 'rm -rf "$buildDir"' EXIT

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) exeSuffix=".exe" ;;
    *) exeSuffix="" ;;
esac

failures=0

for source in "$simDir"/Test*.cpp; do
    name="$(basename "$source" .cpp)"
    exe="$buildDir/$name$exeSuffix"

    if ! "$cxx" -std=c++20 -Wall -Wextra -Werror ${CXXFLAGS:-} \
        -I "$simDir/shim" -I "$vhalRoot/Periphery" -I "$vhalRoot/Common" -I "$simDir" \
        -o "$exe" "$source"; then
        echo "BUILD FAILED: $name"
        failures=$((failures + 1))
        continue
    fi

    if ! "$exe"; then
        echo "TEST FAILED: $name"
        failures=$((failures + 1))
    fi
done

# Every header compiles alone: it is the first header of a translation unit that has nothing else in it. The RTU headers are listed
# by name, because ModbusMaster.h and ModbusSlave.h in the same folder belong to the existing RTU code and are not checked here.
for header in "$modbusDir"/*.h "$modbusDir"/Pdu/*.h \
    "$modbusDir/RTU/RtuFramer.h" "$modbusDir/RTU/RtuTiming.h" "$modbusDir/RTU/RtuTransport.h"; do
    if ! "$cxx" -std=c++20 -Wall -Wextra -Werror -fsyntax-only \
        -I "$simDir/shim" -I "$vhalRoot/Periphery" -I "$vhalRoot/Common" \
        -include "$header" -x c++ /dev/null; then
        echo "HEADER DOES NOT COMPILE ALONE: ${header#"$vhalRoot"/}"
        failures=$((failures + 1))
    fi
done

if ! bash "$simDir/BuilderErrors/check.sh"; then
    echo "BUILDER ERROR CHECK FAILED"
    failures=$((failures + 1))
fi

if [ "$failures" -ne 0 ]; then
    echo "$failures test executable(s) failed"
    exit 1
fi

echo "all test executables passed"
