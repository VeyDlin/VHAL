#!/usr/bin/env bash
# The object check of the IQ core: each sample in ObjectCheck/ is compiled to an object for Cortex-M0+ with -Os, and the
# check fails when the object references a soft-float or soft-double helper (__aeabi_f* or __aeabi_d*). The IQ types must
# not reach either helper. A control sample, control_*.cpp, must reference one, so the check is known to see the helpers.
# Exits non-zero on a build error, a sample that references a helper, or a control that does not.
# Needs arm-none-eabi-g++ and arm-none-eabi-nm on PATH. Objects go to a temporary directory outside the repository.
set -u

simDir="$(cd "$(dirname "$0")" && pwd)"
vhalRoot="$(cd "$simDir/../../../../../.." && pwd)"
armCxx="${ARM_CXX:-arm-none-eabi-g++}"
armNm="${ARM_NM:-arm-none-eabi-nm}"
sampleDir="$simDir/ObjectCheck"
buildDir="$(mktemp -d)"
trap 'rm -rf "$buildDir"' EXIT

failures=0

for source in "$sampleDir"/*.cpp; do
    name="$(basename "$source" .cpp)"
    object="$buildDir/$name.o"

    if ! "$armCxx" -std=c++23 -c -Os -mthumb -mcpu=cortex-m0plus -fno-exceptions -fno-rtti \
        -ffunction-sections -fdata-sections \
        -I "$vhalRoot/Common" -I "$sampleDir/shim" \
        -o "$object" "$source"; then
        echo "BUILD FAILED: $name"
        failures=$((failures + 1))
        continue
    fi

    helpers="$("$armNm" "$object" | grep -E '__aeabi_[fd]' || true)"

    case "$name" in
        control_*)
            if [ -z "$helpers" ]; then
                echo "CONTROL BLIND: $name references no soft-float helper, the check proves nothing"
                failures=$((failures + 1))
            else
                echo "control ok: $name references soft float"
            fi
            ;;
        *)
            if [ -n "$helpers" ]; then
                echo "SOFT FLOAT: $name"
                echo "$helpers"
                failures=$((failures + 1))
            else
                echo "ok: $name"
            fi
            ;;
    esac
done

if [ "$failures" -ne 0 ]; then
    echo "$failures object check(s) failed"
    exit 1
fi

echo "all IQ object checks passed"
