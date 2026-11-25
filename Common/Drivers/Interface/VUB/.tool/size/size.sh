#!/usr/bin/env bash
# Measures what the VUB configurations cost on a Cortex-M4 and a Cortex-M0+ target, in bytes. Each configuration is a program made of
# one ConfigX.cpp (or ConfigCompact.cpp for the CompactStreamingProtocol comparison), SizeMain.cpp and the baseline of its link kind
# (BaselineStream.cpp or BaselinePacket.cpp). The baseline makes the same calls with no VUB, so the cost of a configuration is the
# program minus its baseline. Each program is compiled with arm-none-eabi-g++ -std=c++20 -Os -ffunction-sections -fdata-sections
# -fno-exceptions -fno-rtti -Wl,--gc-sections --specs=nano.specs --specs=nosys.specs, and arm-none-eabi-size reads text, data and bss.
# RAM is data plus bss. Prints a markdown table of the costs, then the toolchain and the baseline sizes. Exits 0 when the toolchain is
# not on the PATH (it is a tool, not a test), and 1 when a program does not build. Works in Git Bash on Windows and on Linux or macOS.
# Build output goes to a temporary directory outside the repository.
#
# CXX and SIZE pick other tools, for example CXX=arm-none-eabi-g++-14 SIZE=arm-none-eabi-size-14.
set -u

sizeDir="$(cd "$(dirname "$0")" && pwd)"
vhalRoot="$(cd "$sizeDir/../../../../../.." && pwd)"
cxx="${CXX:-arm-none-eabi-g++}"
sizeTool="${SIZE:-arm-none-eabi-size}"

if ! command -v "$cxx" >/dev/null 2>&1; then
	echo "$cxx is not on the PATH: the VUB size table was not measured"
	exit 0
fi

buildDir="$(mktemp -d)"
trap 'rm -rf "$buildDir"' EXIT

flags=(-std=c++20 -mthumb -Os -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -Wall -Wextra -Werror)
linkFlags=(-Wl,--gc-sections --specs=nano.specs --specs=nosys.specs)
includes=(-I "$sizeDir/shim" -I "$sizeDir" -I "$vhalRoot/Common" -I "$vhalRoot/Periphery")
cpus=(cortex-m4 cortex-m0plus)
failures=0

declare -A sizes=()

# Builds the configuration's program for one CPU, with the flags of its row (for example -DSIZE_ROLE=1), and stores "text data bss" in sizes.
# Returns 1 when a file does not build.
measure() {
	local cpu="$1"
	local configSource="$2"
	local rowFlags="$3"
	local key="$cpu $configSource $rowFlags"
	local name
	local tag
	local objects=()
	local defines=()
	local source
	local object
	local exe

	if [ -n "${sizes[$key]+set}" ]; then
		return 0
	fi

	name="$(basename "$configSource" .cpp)"
	tag="$(printf '%s' "$rowFlags" | tr -c 'A-Za-z0-9' '_')"
	exe="$buildDir/$cpu-$name$tag.elf"
	read -r -a defines <<< "$rowFlags"

	for source in "$configSource" SizeMain.cpp; do
		object="$buildDir/$cpu-$name$tag-$(basename "$source" .cpp).o"
		local extra=()
		if [ "$source" = "$configSource" ]; then
			extra=(${defines[@]+"${defines[@]}"})
		fi

		if ! "$cxx" "${flags[@]}" -mcpu="$cpu" "${includes[@]}" ${extra[@]+"${extra[@]}"} -c -o "$object" "$sizeDir/$source"; then
			echo "BUILD FAILED: $source for $cpu" >&2
			return 1
		fi

		objects+=("$object")
	done

	# The nosys stubs of the C library warn that they are not implemented; the warnings are not shown unless the link fails.
	local linkLog="$buildDir/$cpu-$name$tag.link.log"
	if ! "$cxx" "${flags[@]}" -mcpu="$cpu" "${objects[@]}" -o "$exe" "${linkFlags[@]}" >"$linkLog" 2>&1; then
		cat "$linkLog" >&2
		echo "LINK FAILED: $configSource $rowFlags for $cpu" >&2
		return 1
	fi

	local text data bss
	read -r text data bss _ < <("$sizeTool" --format=berkeley "$exe" | tail -n 1)
	sizes[$key]="$text $data $bss"
}

# The rows: the label, the baseline, its flags, the configuration, and its flags. A row whose configuration is the requests layer
# (ConfigRequests.cpp) takes the same chain with SIZE_ROLE=0 as its baseline: the cost is the layer on that chain.
rows=(
	"(a) packet link only|BaselinePacket.cpp||ConfigA.cpp|"
	"(b) stream link, COBS|BaselineStream.cpp||ConfigB.cpp|"
	"(c) (b) + CRC-16/MODBUS table|BaselineStream.cpp||ConfigC.cpp|"
	"(d) (b) + CRC-16/MODBUS bitwise|BaselineStream.cpp||ConfigD.cpp|"
	"(e) stream link, length prefix 1, CRC-8 bitwise|BaselineStream.cpp||ConfigE.cpp|"
	"(f) stream link, gap framing, CRC-16 table|BaselineStream.cpp||ConfigF.cpp|"
	"(g) (c) + VUB::Reliable (Retries 3, Window 4)|BaselineStream.cpp||ConfigG.cpp|"
	"(h) (g) with the in-order option|BaselineStream.cpp||ConfigH.cpp|"
	"(i) COBS, CRC-8, VUB::ReliableOf window 1, Retries 3|BaselineStream.cpp||ConfigI.cpp|"
	"breakdown of (i): the same without VUB::ReliableOf|BaselineStream.cpp||ConfigIPlain.cpp|"
	"CompactStreamingProtocol<20, 4> (bootloader use)|BaselinePacket.cpp||ConfigCompact.cpp|"
	"(j) stream link, COBS, CRC-16/MODBUS table, VUB::KeepAlive (default times)|BaselineStream.cpp||ConfigJ.cpp|"
	"(k) (g) + VUB::KeepAlive (default times)|BaselineStream.cpp||ConfigK.cpp|"
	"requests responder only, CRC-16/MODBUS over COBS over stream link|ConfigRequests.cpp|-DSIZE_BASE=1 -DSIZE_ROLE=0|ConfigRequests.cpp|-DSIZE_BASE=1 -DSIZE_ROLE=1"
	"requests requester only (one Send site), CRC-16/MODBUS over COBS over stream link|ConfigRequests.cpp|-DSIZE_BASE=1 -DSIZE_ROLE=0|ConfigRequests.cpp|-DSIZE_BASE=1 -DSIZE_ROLE=2"
	"requests both, CRC-16/MODBUS over COBS over stream link|ConfigRequests.cpp|-DSIZE_BASE=1 -DSIZE_ROLE=0|ConfigRequests.cpp|-DSIZE_BASE=1 -DSIZE_ROLE=3"
	"requests requester only (four Send sites), CRC-16/MODBUS over COBS over stream link|ConfigRequests.cpp|-DSIZE_BASE=1 -DSIZE_ROLE=0|ConfigRequests.cpp|-DSIZE_BASE=1 -DSIZE_ROLE=2 -DSIZE_SENDS=4"
	"requests responder only, COBS over stream link|ConfigRequests.cpp|-DSIZE_BASE=2 -DSIZE_ROLE=0|ConfigRequests.cpp|-DSIZE_BASE=2 -DSIZE_ROLE=1"
	"requests requester only (one Send site), COBS over stream link|ConfigRequests.cpp|-DSIZE_BASE=2 -DSIZE_ROLE=0|ConfigRequests.cpp|-DSIZE_BASE=2 -DSIZE_ROLE=2"
	"requests both, COBS over stream link|ConfigRequests.cpp|-DSIZE_BASE=2 -DSIZE_ROLE=0|ConfigRequests.cpp|-DSIZE_BASE=2 -DSIZE_ROLE=3"
	"requests responder only, ReliableOf<40, 4>|ConfigRequests.cpp|-DSIZE_BASE=3 -DSIZE_ROLE=0|ConfigRequests.cpp|-DSIZE_BASE=3 -DSIZE_ROLE=1"
	"requests requester only (one Send site), ReliableOf<40, 4>|ConfigRequests.cpp|-DSIZE_BASE=3 -DSIZE_ROLE=0|ConfigRequests.cpp|-DSIZE_BASE=3 -DSIZE_ROLE=2"
	"requests both, ReliableOf<40, 4>|ConfigRequests.cpp|-DSIZE_BASE=3 -DSIZE_ROLE=0|ConfigRequests.cpp|-DSIZE_BASE=3 -DSIZE_ROLE=3"
	"health asks only (Health, requester Open(2)), ReliableOf<40, 4>|ConfigHealth.cpp|-DSIZE_LAYER=2|ConfigHealth.cpp|-DSIZE_LAYER=2 -DSIZE_HEALTH=1"
	"health answers next to a served command, with the counters, ReliableOf<40, 4>|ConfigHealth.cpp|-DSIZE_LAYER=1 -DSIZE_COUNTED=1|ConfigHealth.cpp|-DSIZE_LAYER=1 -DSIZE_COUNTED=1 -DSIZE_HEALTH=2"
	"health both, next to served and requester, with the counters, ReliableOf<40, 4>|ConfigHealth.cpp|-DSIZE_LAYER=3 -DSIZE_COUNTED=1|ConfigHealth.cpp|-DSIZE_LAYER=3 -DSIZE_COUNTED=1 -DSIZE_HEALTH=3"
	"health next to ping (both sides), with the counters, ReliableOf<40, 4>|ConfigHealth.cpp|-DSIZE_LAYER=2 -DSIZE_COUNTED=1 -DSIZE_PING=1|ConfigHealth.cpp|-DSIZE_LAYER=2 -DSIZE_COUNTED=1 -DSIZE_PING=1 -DSIZE_HEALTH=3"
	"the counters alone, next to a served command, ReliableOf<40, 4>|ConfigHealth.cpp|-DSIZE_LAYER=1|ConfigHealth.cpp|-DSIZE_LAYER=1 -DSIZE_COUNTED=1"
)

baselines=(BaselinePacket.cpp BaselineStream.cpp)

printf '| Configuration | M4 text | M4 RAM | M0+ text | M0+ RAM |\n'
printf '|---|---:|---:|---:|---:|\n'

for row in "${rows[@]}"; do
	IFS='|' read -r label baseSource baseFlags configSource configFlags <<< "$row"
	line="| $label"

	for cpu in "${cpus[@]}"; do
		if ! measure "$cpu" "$baseSource" "$baseFlags" || ! measure "$cpu" "$configSource" "$configFlags"; then
			failures=$((failures + 1))
			line="$line | failed | failed"
			continue
		fi

		read -r baseText baseData baseBss <<< "${sizes[$cpu $baseSource $baseFlags]}"
		read -r text data bss <<< "${sizes[$cpu $configSource $configFlags]}"
		line="$line | $((text - baseText)) | $((data + bss - baseData - baseBss))"
	done

	printf '%s |\n' "$line"
done

echo
"$cxx" --version | head -n 1 | sed 's/^/Toolchain: /'
for baseSource in "${baselines[@]}"; do
	for cpu in "${cpus[@]}"; do
		if measure "$cpu" "$baseSource" ""; then
			read -r text data bss <<< "${sizes[$cpu $baseSource ]}"
			echo "Baseline $baseSource on $cpu: text $text, data $data, bss $bss"
		fi
	done
done

if [ "$failures" -ne 0 ]; then
	echo "$failures row(s) did not build"
	exit 1
fi
