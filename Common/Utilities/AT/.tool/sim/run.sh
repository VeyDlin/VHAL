#!/usr/bin/env bash
# Builds every host test of the AT description and value grammar and runs it, checks the description's rules at compile time, and
# compiles the scale set with the host compiler and with the target compiler (-fsyntax-only). Exits non-zero when a test fails to build
# or fails, when a snippet does not fail with its message, or when the host scale set does not compile. Works in Git Bash on Windows and
# on Linux or macOS. Build output goes to a temporary directory outside the repository.
#
# The target check uses ARM_CXX, or arm-none-eabi-g++ from PATH. Without it the check prints a skip line and does not fail, so a host
# without the toolchain can still run the rest. The compiler version is written into the log.
set -u

simDir="$(cd "$(dirname "$0")" && pwd)"
vhalRoot="$(cd "$simDir/../../../../.." && pwd)"
cxx="${CXX:-g++}"
armCxx="${ARM_CXX:-arm-none-eabi-g++}"
buildDir="$(mktemp -d)"
trap 'rm -rf "$buildDir"' EXIT

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) exeSuffix=".exe" ;;
    *) exeSuffix="" ;;
esac

failures=0

echo "host compiler: $("$cxx" --version | head -1)"

for source in "$simDir"/Test*.cpp; do
    name="$(basename "$source" .cpp)"
    exe="$buildDir/$name$exeSuffix"

    if ! "$cxx" -std=c++23 -Wall -Wextra -Werror -pthread \
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

if ! bash "$simDir/BuilderErrors/check.sh"; then
    echo "BUILDER ERROR CHECK FAILED"
    failures=$((failures + 1))
fi

if ! "$cxx" -std=c++23 -Wall -Wextra -Werror -c \
    -I "$simDir/shim" -I "$vhalRoot/Periphery" -I "$vhalRoot/Common" \
    -o "$buildDir/ScaleSet.o" "$simDir/ScaleSet.cpp"; then
    echo "SCALE SET FAILED: host"
    failures=$((failures + 1))
else
    echo "scale set compiles on the host"
fi

if ! "$cxx" -std=c++23 -Wall -Wextra -Werror -c \
    -I "$simDir/shim" -I "$vhalRoot/Periphery" -I "$vhalRoot/Common" \
    -o "$buildDir/DialectAll.o" "$simDir/DialectAll.cpp"; then
    echo "DIALECT SET FAILED: host (with every dialect item)"
    failures=$((failures + 1))
fi

if command -v "$armCxx" > /dev/null 2>&1; then
    echo "target compiler: $("$armCxx" --version | head -1)"
    armSize="$(dirname "$(command -v "$armCxx")")/arm-none-eabi-size"
    if ! "$armCxx" -std=gnu++23 -fno-rtti -fno-exceptions -Wall -Wextra -Werror -fsyntax-only \
        -I "$simDir/shim" -I "$vhalRoot/Periphery" -I "$vhalRoot/Common" \
        "$simDir/ScaleSet.cpp"; then
        echo "SCALE SET FAILED: target"
        failures=$((failures + 1))
    else
        echo "scale set passes the target syntax check"
    fi

    # The dialect's flash cost: the same server with no dialect item and with every item, both at -Os for Cortex-M4. The difference is
    # the dialect.
    for dialect in DialectNone DialectAll; do
        if ! "$armCxx" -std=gnu++23 -fno-rtti -fno-exceptions -Os -mcpu=cortex-m4 -mthumb -Wall -Wextra -Werror -c \
            -I "$simDir/shim" -I "$vhalRoot/Periphery" -I "$vhalRoot/Common" \
            -o "$buildDir/$dialect.o" "$simDir/$dialect.cpp"; then
            echo "DIALECT SET FAILED: target $dialect"
            failures=$((failures + 1))
        fi
    done

    if [ -f "$buildDir/DialectNone.o" ] && [ -f "$buildDir/DialectAll.o" ]; then
        echo "dialect flash, -Os for Cortex-M4, object text size (no dialect item, every dialect item):"
        if ! "$armSize" "$buildDir/DialectNone.o" "$buildDir/DialectAll.o"; then
            echo "SIZE FAILED: $armSize"
            failures=$((failures + 1))
        fi
    fi

    # The client and the server of one five-command set, each compiled alone at -Os for Cortex-M4, so each object's text, data and bss
    # is its own side's code.
    for side in SizeServer SizeClient; do
        if ! "$armCxx" -std=gnu++23 -fno-rtti -fno-exceptions -Os -mcpu=cortex-m4 -mthumb -Wall -Wextra -Werror -c \
            -I "$simDir/shim" -I "$vhalRoot/Periphery" -I "$vhalRoot/Common" -I "$simDir" \
            -o "$buildDir/$side.o" "$simDir/$side.cpp"; then
            echo "SIZE OBJECT FAILED: target $side"
            failures=$((failures + 1))
        fi
    done

    # The ESP-AT client of the description, each object with one module set: the echo and row modules, the same with the await module, and
    # neither module. The calls are the same in each, except that the rows call needs the row module, and the await calls are in the object
    # with the await module only.
    for side in SizeEspAtRows SizeEspAtAwait SizeEspAtPlain; do
        if ! "$armCxx" -std=gnu++23 -fno-rtti -fno-exceptions -Os -mcpu=cortex-m4 -mthumb -Wall -Wextra -Werror -c \
            -I "$simDir/shim" -I "$vhalRoot/Periphery" -I "$vhalRoot/Common" -I "$simDir" \
            -o "$buildDir/$side.o" "$simDir/$side.cpp"; then
            echo "SIZE OBJECT FAILED: target $side"
            failures=$((failures + 1))
        fi
    done

    if [ -f "$buildDir/SizeEspAtRows.o" ] && [ -f "$buildDir/SizeEspAtAwait.o" ] && [ -f "$buildDir/SizeEspAtPlain.o" ]; then
        echo "ESP-AT client, -Os for Cortex-M4 (text, data, bss): echo and rows; the same with await; neither module:"
        if ! "$armSize" "$buildDir/SizeEspAtRows.o" "$buildDir/SizeEspAtAwait.o" "$buildDir/SizeEspAtPlain.o"; then
            echo "SIZE FAILED: $armSize"
            failures=$((failures + 1))
        fi
    fi

    if [ -f "$buildDir/SizeServer.o" ] && [ -f "$buildDir/SizeClient.o" ]; then
        echo "server and client of a five-command set, -Os for Cortex-M4 (text, data, bss):"
        if ! "$armSize" "$buildDir/SizeServer.o" "$buildDir/SizeClient.o"; then
            echo "SIZE FAILED: $armSize"
            failures=$((failures + 1))
        fi
    fi
else
    echo "SKIPPED target check: no compiler named $armCxx (set ARM_CXX)"
fi

if [ "$failures" -ne 0 ]; then
    echo "$failures check(s) failed"
    exit 1
fi

echo "all AT checks passed"
