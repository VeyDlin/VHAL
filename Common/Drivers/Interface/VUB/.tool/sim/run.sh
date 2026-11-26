#!/usr/bin/env bash
# Builds every host test of the VUB core and runs it with g++, checks that every VUB header compiles alone, and checks the builder
# error snippets. Exits non-zero when a test fails to build or fails. Works in Git Bash on Windows and on Linux or macOS. Build output
# goes to a temporary directory outside the repository.
#
# CXXFLAGS adds flags to the test builds, for example CXXFLAGS="-fsanitize=address,undefined" where the toolchain has the sanitizer
# libraries. The default run does not use sanitizers.
#
# A test names the libraries it links on each system in one of its first three lines: "// libs-windows: ws2_32 setupapi" and
# "// libs-linux: usb-1.0". Each name is passed to the linker as -l<name>, on the system the script runs on.
set -u

simDir="$(cd "$(dirname "$0")" && pwd)"
vhalRoot="$(cd "$simDir/../../../../../.." && pwd)"
vubDir="$vhalRoot/Common/Drivers/Interface/VUB"
cxx="${CXX:-g++}"
buildDir="$(mktemp -d)"
trap 'rm -rf "$buildDir"' EXIT

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) exeSuffix=".exe"; platform="windows" ;;
    Linux*) exeSuffix=""; platform="linux" ;;
    *) exeSuffix=""; platform="other" ;;
esac

failures=0

for source in "$simDir"/Test*.cpp; do
    name="$(basename "$source" .cpp)"
    exe="$buildDir/$name$exeSuffix"

    # The test of the real USB functions needs the USB module's simulation shim, which defines VHAL.h and the assertions of its own.
    # Its flags come first, so its VHAL.h is the one that test gets.
    extraFlags=()
    if [ "$name" = "TestVubLinkUsb" ] || [ "$name" = "TestVubLinkWiki" ]; then
        usbSim="$vhalRoot/Common/Drivers/Interface/USB/.tool/sim"
        extraFlags=(-I "$usbSim/shim" -I "$usbSim" -I "$vhalRoot/Common/Drivers/Interface/USB/.tool/common" -I "$vhalRoot/.demo/STM32G4/Application")
    fi

    linkLibs=""
    lineNumber=0
    while [ "$lineNumber" -lt 3 ] && IFS= read -r line; do
        lineNumber=$((lineNumber + 1))
        # A checkout with core.autocrlf gives the sources CRLF line ends; the CR is not part of a library name.
        line="${line%$'\r'}"
        case "$line" in
            "// libs-$platform:"*) linkLibs="${line#"// libs-$platform:"}" ;;
        esac
    done < "$source"

    linkFlags=()
    for lib in $linkLibs; do
        linkFlags+=("-l$lib")
    done

    if ! "$cxx" -std=c++23 -Wall -Wextra -Werror ${CXXFLAGS:-} ${extraFlags[@]+"${extraFlags[@]}"} \
        -I "$vhalRoot/Common" -I "$simDir" \
        -I "$vhalRoot/Periphery/Adapter/.tool/sim/shim" -I "$vhalRoot/Periphery" \
        -o "$exe" "$source" ${linkFlags[@]+"${linkFlags[@]}"}; then
        echo "BUILD FAILED: $name"
        failures=$((failures + 1))
        continue
    fi

    if ! "$exe"; then
        echo "TEST FAILED: $name"
        failures=$((failures + 1))
    fi
done

# Every header compiles alone: it is the first header of a translation unit that has nothing else in it.
# Every header of every block folder; a folder that does not exist yet adds nothing. The PC links compile on the system the script
# runs on: Windows or Linux. The microcontroller links take System.h from Periphery, through the host shim the tests use.
shopt -s nullglob
for header in "$vubDir"/*.h "$vubDir"/{TestKit,Link,Framing,Transform,Engine,Layer,PC,Requests}/*.h; do
    if ! "$cxx" -std=c++23 -Wall -Wextra -Werror -fsyntax-only \
        -I "$vhalRoot/Common" -I "$vhalRoot/Periphery/Adapter/.tool/sim/shim" -I "$vhalRoot/Periphery" \
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
