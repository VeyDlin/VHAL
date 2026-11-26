#!/usr/bin/env bash
# Checks that an unused CRC preset costs nothing. A Cortex-M4 image that includes all the preset files and uses one of them must be the
# same size, in text and in RAM, as the image that includes only that one file. Both are built at -Os and -O2 with arm-none-eabi-g++
# (CXX picks another compiler) and linked without the C library, so the sizes are only the code and data of the CRC call. Exits 1 when
# the sizes differ or a file does not build, and 0 when the toolchain is not on the PATH.
set -u

sizeDir="$(cd "$(dirname "$0")" && pwd)"
vhalRoot="$(cd "$sizeDir/../../../../../.." && pwd)"
cxx="${CXX:-arm-none-eabi-g++}"
sizeTool="${SIZE:-arm-none-eabi-size}"

if ! command -v "$cxx" >/dev/null 2>&1; then
	echo "$cxx is not on the PATH: the preset size check was not run"
	exit 0
fi

buildDir="$(mktemp -d)"
trap 'rm -rf "$buildDir"' EXIT

flags=(-std=c++23 -mcpu=cortex-m4 -mthumb -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -Wall -Wextra -Werror)
includes=(-I "$vhalRoot/Common")
failures=0

# Builds and links one image for an optimisation, and prints "text ram" of the program. RAM is data plus bss.
measure() {
	local opt="$1"
	local source="$2"
	local name="$3"
	local object="$buildDir/$name-$opt.o"
	local exe="$buildDir/$name-$opt.elf"
	local text data bss

	if ! "$cxx" "${flags[@]}" -"$opt" "${includes[@]}" -c -o "$object" "$source"; then
		echo "BUILD FAILED: $(basename "$source") at -$opt" >&2
		return 1
	fi

	if ! "$cxx" -mcpu=cortex-m4 -mthumb -"$opt" -nostdlib -Wl,--gc-sections -Wl,-e,SizePresetsEntry -o "$exe" "$object"; then
		echo "LINK FAILED: $(basename "$source") at -$opt" >&2
		return 1
	fi

	read -r text data bss _ < <("$sizeTool" --format=berkeley "$exe" | tail -n 1)
	echo "$text $((data + bss))"
}

printf '| Optimisation | Image | M4 text | M4 RAM |\n'
printf '|---|---|---:|---:|\n'

for opt in Os O2; do
	one="$(measure "$opt" "$sizeDir/SizePresetsOne.cpp" one)" || {
		failures=$((failures + 1))
		continue
	}

	all="$(measure "$opt" "$sizeDir/SizePresetsAll.cpp" all)" || {
		failures=$((failures + 1))
		continue
	}

	read -r oneText oneRam <<< "$one"
	read -r allText allRam <<< "$all"
	printf '| -%s | one preset file | %s | %s |\n' "$opt" "$oneText" "$oneRam"
	printf '| -%s | all preset files, one used | %s | %s |\n' "$opt" "$allText" "$allRam"

	if [ "$oneText" -ne "$allText" ] || [ "$oneRam" -ne "$allRam" ]; then
		echo "PRESETS COST: -$opt image with all presets is $allText text and $allRam RAM, with one file $oneText and $oneRam" >&2
		failures=$((failures + 1))
	fi
done

echo
"$cxx" --version | head -n 1

if [ "$failures" -ne 0 ]; then
	echo "$failures preset size check(s) failed"
	exit 1
fi
