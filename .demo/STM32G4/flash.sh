#!/bin/sh
# Writes the benchmark firmware to the STM32G431 over SWD with STM32CubeProgrammer, verifies it and resets the board.
#
# Usage: sh flash.sh [path/to/USBBenchG431.bin]
#   Default image: build/RelSpeed/USBBenchG431.bin next to this script (build the RelSpeed preset first).
#
# Environment:
#   STM32_PROGRAMMER_CLI  full path of STM32_Programmer_CLI.exe
#                         (default: the STM32CubeProgrammer install folder)
#   STM32_SWD_SN          ST-LINK serial number, only needed when more than one probe is connected

set -eu

script_dir=$(cd "$(dirname "$0")" && pwd)
cli="${STM32_PROGRAMMER_CLI:-C:/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe}"
image="${1:-$script_dir/build/RelSpeed/USBBenchG431.bin}"
address=0x08000000

if [ ! -f "$image" ]; then
    echo "flash.sh: image not found: $image (build the RelSpeed preset first)" >&2
    exit 1
fi

if [ ! -f "$cli" ]; then
    echo "flash.sh: STM32_Programmer_CLI not found: $cli (set STM32_PROGRAMMER_CLI)" >&2
    exit 1
fi

if [ -n "${STM32_SWD_SN:-}" ]; then
    "$cli" -c port=SWD "sn=$STM32_SWD_SN" -w "$image" "$address" -v -rst
else
    "$cli" -c port=SWD -w "$image" "$address" -v -rst
fi
