#!/usr/bin/env bash
# Checks that each call of Matches, Contains and Find keeps its workspace in its own frame. FrameCaller.cpp makes the six calls of two
# patterns of different sizes from one function, and it is compiled for the device with -O3 and -fstack-usage. The stack usage file then
# has to show:
#   - the caller frame is under CallerFrameLimitBytes, so the caller holds no workspace;
#   - the frames of the two Matches, the two Contains and the two Find are paired by size, the smallest with the smallest, and each frame
#     is at least the workspace of its pattern and at most that workspace plus FrameSlackBytes.
# The lower bound shows the workspace is in the frame. The slack covers the result object and the loop locals that are not workspace:
# the largest one measured for Find is 224 bytes.
# Without the entry points marked noinline, GCC folds the calls into the caller, so the caller frame is over the limit and the Find lines
# are missing; the check then fails. The optional argument is the Common directory to check, and it defaults to the one of this repository.
# Needs arm-none-eabi-g++ on the PATH. Exits non-zero when a check fails.
set -u

simDir="$(cd "$(dirname "$0")" && pwd)"
vhalRoot="$(cd "$simDir/../../../../.." && pwd)"
commonDir="${1:-$vhalRoot/Common}"
cxx="${CXX:-g++}"
buildDir="$(mktemp -d)"
trap 'rm -rf "$buildDir"' EXIT

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) exeSuffix=".exe" ;;
    *) exeSuffix="" ;;
esac

callerFrameLimitBytes=128
frameSlackBytes=256
failures=0

if ! command -v arm-none-eabi-g++ > /dev/null 2>&1; then
    echo "SKIPPED: frame check (arm-none-eabi-g++ is not on the PATH)"
    exit 0
fi

# The workspace sizes come from a host build of the same patterns. They count arrays of 2- and 4-byte elements, so they match the device.
if ! "$cxx" -std=c++23 -Wall -Wextra -Werror -I "$commonDir" -o "$buildDir/FrameSizes$exeSuffix" "$simDir/FrameSizes.cpp"; then
    echo "BUILD FAILED: FrameSizes"
    exit 1
fi

"$buildDir/FrameSizes$exeSuffix" > "$buildDir/sizes.txt" || exit 1

if ! arm-none-eabi-g++ -std=gnu++23 -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -fno-rtti -fno-exceptions \
    -fdata-sections -ffunction-sections -fstack-usage -O3 -Wall -Wextra -Werror -I "$commonDir" \
    -c "$simDir/FrameCaller.cpp" -o "$buildDir/FrameCaller.o"; then
    echo "BUILD FAILED: FrameCaller"
    exit 1
fi

suFile="$buildDir/FrameCaller.su"

# The frame of the caller, and the frames of the entry points, one list per entry point name.
mapfile -t callerList < <(grep -F 'FrameCaller(' "$suFile" | cut -f2)
if [ "${#callerList[@]}" -ne 1 ]; then
    echo "CHECK FAILED: FrameCaller is not one function in the stack usage file"
    exit 1
fi

callerFrame="${callerList[0]}"
if [ "$callerFrame" -ge "$callerFrameLimitBytes" ]; then
    echo "CHECK FAILED: caller frame $callerFrame bytes, limit $callerFrameLimitBytes (a workspace is in the caller)"
    failures=$((failures + 1))
else
    echo "caller frame $callerFrame bytes, limit $callerFrameLimitBytes"
fi

# CheckKind compares the frames of one entry point with the workspace sizes of one kind, smallest with smallest.
CheckKind() {
    local entry="$1"
    local kind="$2"
    local frameList=()
    local workspaceList=()
    mapfile -t frameList < <(grep -F "$entry" "$suFile" | cut -f2 | sort -n)
    mapfile -t workspaceList < <(grep "^$kind " "$buildDir/sizes.txt" | cut -d' ' -f2 | sort -n)
    if [ "${#frameList[@]}" -ne 2 ] || [ "${#workspaceList[@]}" -ne 2 ]; then
        echo "CHECK FAILED: $entry has ${#frameList[@]} frames, expected 2"
        return 1
    fi

    local index
    local result=0
    for index in 0 1; do
        local frame="${frameList[$index]}"
        local workspace="${workspaceList[$index]}"
        local limit=$((workspace + frameSlackBytes))
        if [ "$frame" -lt "$workspace" ] || [ "$frame" -gt "$limit" ]; then
            echo "CHECK FAILED: $entry frame $frame bytes, workspace $workspace, allowed $workspace to $limit"
            result=1
        else
            echo "$entry frame $frame bytes, workspace $workspace, allowed $workspace to $limit"
        fi
    done

    return "$result"
}

if ! CheckKind '::Matches(' plain; then
    failures=$((failures + 1))
fi

if ! CheckKind '::Contains(' plain; then
    failures=$((failures + 1))
fi

if ! CheckKind '::Find(' captures; then
    failures=$((failures + 1))
fi

if [ "$failures" -ne 0 ]; then
    echo "frame check failed"
    exit 1
fi

echo "frame check passed"
