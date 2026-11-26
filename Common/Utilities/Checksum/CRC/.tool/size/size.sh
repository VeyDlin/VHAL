#!/usr/bin/env bash
# Measures what each CRC engine costs on a Cortex-M4 and a Cortex-M0+ target, in bytes, for one algorithm (CRC-16/MODBUS) and for three
# (CRC-16/MODBUS, CRC-32/ISO-HDLC and CRC-8/SMBUS), at -Os and -O2. Each cost is a program with the engine's calls minus the baseline
# program, which makes the same call with no CRC, so the table gives the cost of the engine and not of the surrounding code. Text is
# the code and the flash tables; RAM is data plus bss, and the engines keep no RAM, so it is 0 in every row.
#
# The table is compared with the limits written below. The script exits 1 when a cost grows above its limit, and 1 when a program does
# not build. The bitwise engine is also measured at -O3: a 1 KB table would be the compiler's -foptimize-crc, and the limit catches it.
# Limits are in bytes of text. Prints a markdown table, then the toolchain. Exits 0 when the toolchain is not on the PATH.
#
# arm-none-eabi-g++ is the compiler by default; CXX and SIZE pick other tools, for example CXX=arm-none-eabi-g++-14.
set -u

sizeDir="$(cd "$(dirname "$0")" && pwd)"
vhalRoot="$(cd "$sizeDir/../../../../../.." && pwd)"
cxx="${CXX:-arm-none-eabi-g++}"
sizeTool="${SIZE:-arm-none-eabi-size}"

if ! command -v "$cxx" >/dev/null 2>&1; then
	echo "$cxx is not on the PATH: the CRC size table was not measured"
	exit 0
fi

buildDir="$(mktemp -d)"
trap 'rm -rf "$buildDir"' EXIT

flags=(-std=c++23 -mthumb -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -Wall -Wextra -Werror)
linkFlags=(-Wl,--gc-sections --specs=nano.specs --specs=nosys.specs)
includes=(-I "$sizeDir" -I "$vhalRoot/Common")
cpus=(cortex-m4 cortex-m0plus)
engines=(CrcBitwise CrcNibble CrcTable)
algorithmCounts=(1 3)
failures=0

declare -A sizes=()
declare -A limits=()

# The text limits, in bytes of cost: key "opt engine algorithms cpu". The bitwise limits are the measured costs of the engine.
limits["Os CrcBitwise 1 cortex-m4"]=24
limits["Os CrcBitwise 1 cortex-m0plus"]=24
limits["Os CrcBitwise 3 cortex-m4"]=112
limits["Os CrcBitwise 3 cortex-m0plus"]=116
limits["Os CrcNibble 1 cortex-m4"]=68
limits["Os CrcNibble 1 cortex-m0plus"]=64
limits["Os CrcNibble 3 cortex-m4"]=260
limits["Os CrcNibble 3 cortex-m0plus"]=264
limits["Os CrcTable 1 cortex-m4"]=528
limits["Os CrcTable 1 cortex-m0plus"]=528
limits["Os CrcTable 3 cortex-m4"]=1876
limits["Os CrcTable 3 cortex-m0plus"]=1876
limits["O2 CrcBitwise 1 cortex-m4"]=36
limits["O2 CrcBitwise 1 cortex-m0plus"]=24
limits["O2 CrcBitwise 3 cortex-m4"]=124
limits["O2 CrcBitwise 3 cortex-m0plus"]=120
limits["O2 CrcNibble 1 cortex-m4"]=76
limits["O2 CrcNibble 1 cortex-m0plus"]=68
limits["O2 CrcNibble 3 cortex-m4"]=268
limits["O2 CrcNibble 3 cortex-m0plus"]=280
limits["O2 CrcTable 1 cortex-m4"]=532
limits["O2 CrcTable 1 cortex-m0plus"]=528
limits["O2 CrcTable 3 cortex-m4"]=1884
limits["O2 CrcTable 3 cortex-m0plus"]=1872
limits["O3 CrcBitwise 1 cortex-m4"]=36
limits["O3 CrcBitwise 1 cortex-m0plus"]=24
limits["O3 CrcBitwise 3 cortex-m4"]=124
limits["O3 CrcBitwise 3 cortex-m0plus"]=120

# Builds one program: the engine and algorithm count given, for one CPU and optimisation. The program's name is the arguments joined.
# Returns 1 when a file does not build or does not link. The compiled objects go to the build directory.
build() {
	local opt="$1"
	local cpu="$2"
	local engine="$3"
	local algorithms="$4"
	local name="$opt-$cpu-$engine-$algorithms"
	local configObject="$buildDir/$name-config.o"
	local exe="$buildDir/$name.elf"

	if ! "$cxx" "${flags[@]}" -"$opt" -mcpu="$cpu" "${includes[@]}" \
		-DSIZE_ENGINE="$engine" -DSIZE_ALGORITHMS="$algorithms" -c -o "$configObject" "$sizeDir/SizeConfig.cpp"; then
		echo "BUILD FAILED: SizeConfig.cpp for $engine, $algorithms algorithm(s), -$opt, $cpu" >&2
		return 1
	fi

	if ! link "$opt" "$cpu" "$configObject" "$exe"; then
		return 1
	fi
}

# Links the program: the SizeMain object of the CPU and optimisation, with the given object, into the executable; its sizes go to sizes.
link() {
	local opt="$1"
	local cpu="$2"
	local object="$3"
	local exe="$4"
	local mainObject="$buildDir/main-$opt-$cpu.o"
	local linkLog="$buildDir/link-$opt-$cpu-$(basename "$exe" .elf).log"
	local text data bss

	if ! "$cxx" "${flags[@]}" -"$opt" -mcpu="$cpu" "${includes[@]}" -c -o "$mainObject" "$sizeDir/SizeMain.cpp"; then
		echo "BUILD FAILED: SizeMain.cpp for -$opt, $cpu" >&2
		return 1
	fi

	# The nosys and nano stubs of the C library warn that they are not implemented; the warnings are kept in the log.
	if ! "$cxx" -mthumb -"$opt" -mcpu="$cpu" "$mainObject" "$object" -o "$exe" "${linkFlags[@]}" >"$linkLog" 2>&1; then
		cat "$linkLog" >&2
		echo "LINK FAILED: $(basename "$exe" .elf)" >&2
		return 1
	fi

	read -r text data bss _ < <("$sizeTool" --format=berkeley "$exe" | tail -n 1)
	sizes[$(basename "$exe" .elf)]="$text $data $bss"
}

# Builds the baseline of the CPU and optimisation: the same main and the baseline function, with no CRC.
baseline() {
	local opt="$1"
	local cpu="$2"
	local baseObject="$buildDir/baseline-$opt-$cpu.o"
	local exe="$buildDir/baseline-$opt-$cpu.elf"

	if ! "$cxx" "${flags[@]}" -"$opt" -mcpu="$cpu" "${includes[@]}" -c -o "$baseObject" "$sizeDir/SizeBaseline.cpp"; then
		echo "BUILD FAILED: SizeBaseline.cpp for -$opt, $cpu" >&2
		return 1
	fi

	link "$opt" "$cpu" "$baseObject" "$exe"
}

# The cost of one row for one CPU: the program's text and RAM minus the baseline's; prints "text ram", or "failed".
cost() {
	local opt="$1"
	local cpu="$2"
	local engine="$3"
	local algorithms="$4"
	local baseKey="baseline-$opt-$cpu"
	local progKey="$opt-$cpu-$engine-$algorithms"
	local baseText baseData baseBss text data bss

	if ! baseline "$opt" "$cpu" || ! build "$opt" "$cpu" "$engine" "$algorithms"; then
		echo "failed"
		return 1
	fi

	read -r baseText baseData baseBss <<< "${sizes[$baseKey]}"
	read -r text data bss <<< "${sizes[$progKey]}"
	echo "$((text - baseText)) $((data + bss - baseData - baseBss))"
}

# Checks one cost against its limit and reports the row when it is over.
check() {
	local opt="$1"
	local engine="$2"
	local algorithms="$3"
	local cpu="$4"
	local text="$5"
	local key="$opt $engine $algorithms $cpu"

	if [ -z "${limits[$key]+set}" ]; then
		echo "NO LIMIT: $key" >&2
		failures=$((failures + 1))
		return 0
	fi

	if [ "$text" -gt "${limits[$key]}" ]; then
		echo "OVER LIMIT: $key is $text bytes of text, the limit is ${limits[$key]}" >&2
		failures=$((failures + 1))
	fi
}

# Measures and prints the table of one optimisation. The bitwise engine at -O3 is measured with the -Os rows and checked alone.
table() {
	local opt="$1"
	local engine
	local algorithms
	local line
	local cpu
	local result
	local text
	local ram

	printf '\n### -%s\n\n' "$opt"
	printf '| Engine | Algorithms | M4 text | M4 RAM | M0+ text | M0+ RAM |\n'
	printf '|---|---:|---:|---:|---:|---:|\n'

	for engine in "${engines[@]}"; do
		for algorithms in "${algorithmCounts[@]}"; do
			line="| $engine | $algorithms"
			for cpu in "${cpus[@]}"; do
				result="$(cost "$opt" "$cpu" "$engine" "$algorithms")" || {
					failures=$((failures + 1))
					line="$line | failed | failed"
					continue
				}

				read -r text ram <<< "$result"
				check "$opt" "$engine" "$algorithms" "$cpu" "$text"
				if [ "$ram" -ne 0 ]; then
					echo "RAM NOT ZERO: $opt $engine $algorithms $cpu is $ram bytes" >&2
					failures=$((failures + 1))
				fi

				line="$line | $text | $ram"
			done

			printf '%s |\n' "$line"
		done
	done
}

table Os
table O2

# The bitwise engine at -O3: the compiler's -foptimize-crc must not turn its loop into a table.
printf '\n### -O3, bitwise only\n\n'
printf '| Engine | Algorithms | M4 text | M4 RAM | M0+ text | M0+ RAM |\n'
printf '|---|---:|---:|---:|---:|---:|\n'
for algorithms in "${algorithmCounts[@]}"; do
	line="| CrcBitwise | $algorithms"
	for cpu in "${cpus[@]}"; do
		result="$(cost O3 "$cpu" CrcBitwise "$algorithms")" || {
			failures=$((failures + 1))
			line="$line | failed | failed"
			continue
		}

		read -r text ram <<< "$result"
		check O3 CrcBitwise "$algorithms" "$cpu" "$text"
		line="$line | $text | $ram"
	done

	printf '%s |\n' "$line"
done

echo
"$cxx" --version | head -n 1 | sed 's/^/Toolchain: /'

if [ "$failures" -ne 0 ]; then
	echo "$failures CRC size check(s) failed"
	exit 1
fi
