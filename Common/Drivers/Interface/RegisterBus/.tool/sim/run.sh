#!/usr/bin/env bash
# Builds and runs the host tests of the register bus over I2C and over SPI, checks that each header compiles alone, and that
# the SPI framing builder error snippets fail with their messages. Works in Git Bash on Windows and on Linux or macOS. Build output
# goes to a temporary directory outside the repository.
set -u

simDir="$(cd "$(dirname "$0")" && pwd)"
vhalRoot="$(cd "$simDir/../../../../../.." && pwd)"
busDir="$vhalRoot/Common/Drivers/Interface/RegisterBus"
cxx="${CXX:-g++}"
buildDir="$(mktemp -d)"
trap 'rm -rf "$buildDir"' EXIT

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) exeSuffix=".exe" ;;
    *) exeSuffix="" ;;
esac

flags=(-std=c++23 -Wall -Wextra -Werror -Wconversion -I "$vhalRoot/Common" -I "$simDir")
failures=0

for source in "$simDir"/Test*.cpp; do
    name="$(basename "$source" .cpp)"
    exe="$buildDir/$name$exeSuffix"

    if ! "$cxx" "${flags[@]}" -o "$exe" "$source"; then
        echo "BUILD FAILED: $name"
        failures=$((failures + 1))
        continue
    fi

    if ! "$exe"; then
        echo "TEST FAILED: $name"
        failures=$((failures + 1))
    fi
done

for header in "$busDir"/*.h; do
    name="$(basename "$header")"
    probe="$buildDir/header_$name.cpp"
    printf '#include <Drivers/Interface/RegisterBus/%s>\n' "$name" > "$probe"
    if ! "$cxx" "${flags[@]}" -fsyntax-only "$probe"; then
        echo "HEADER DOES NOT COMPILE ALONE: $name"
        failures=$((failures + 1))
    fi
done

if ! bash "$simDir/BuilderErrors/check.sh"; then
    failures=$((failures + 1))
fi

if [ "$failures" -ne 0 ]; then
    echo "$failures register bus check(s) failed"
    exit 1
fi

echo "all register bus checks passed"
