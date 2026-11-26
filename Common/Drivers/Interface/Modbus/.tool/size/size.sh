#!/usr/bin/env bash
# Measures the cost of the Modbus probes for a Cortex-M4 target. Each .cpp in this folder is one row of the size tables on the
# ModbusServer, ModbusClient and RtuTransport pages. Each probe is compiled with arm-none-eabi-g++ to an object file, and
# arm-none-eabi-size reads its text, data and bss. The transport and the UART are declared and not defined, so an object holds
# only the code its probe reaches, and no link is needed. Prints one line per probe, in bytes. Exits non-zero when a probe does not
# build. Works in Git Bash on Windows and on Linux or macOS. Build output goes to a temporary directory outside the repository.
#
# CXX and SIZE pick other tools, for example CXX=arm-none-eabi-g++-14 SIZE=arm-none-eabi-size-14.
set -u

sizeDir="$(cd "$(dirname "$0")" && pwd)"
vhalRoot="$(cd "$sizeDir/../../../../../.." && pwd)"
cxx="${CXX:-arm-none-eabi-g++}"
sizeTool="${SIZE:-arm-none-eabi-size}"
buildDir="$(mktemp -d)"
trap 'rm -rf "$buildDir"' EXIT

flags=(-std=gnu++23 -mcpu=cortex-m4 -mthumb -fno-rtti -fno-exceptions -Os -Wall -Wextra -Werror)
failures=0

printf '%-22s %8s %8s %8s\n' probe text data bss

for source in "$sizeDir"/*.cpp; do
    name="$(basename "$source" .cpp)"
    object="$buildDir/$name.o"

    if ! "$cxx" "${flags[@]}" -I "$sizeDir/shim" -I "$sizeDir" -I "$vhalRoot/Common" -I "$vhalRoot/Periphery" \
        -c -o "$object" "$source"; then
        echo "BUILD FAILED: $name"
        failures=$((failures + 1))
        continue
    fi

    read -r text data bss _ < <("$sizeTool" --format=berkeley "$object" | tail -n 1)
    printf '%-22s %8s %8s %8s\n' "$name" "$text" "$data" "$bss"
done

if [ "$failures" -ne 0 ]; then
    echo "$failures probe(s) failed to build"
    exit 1
fi
