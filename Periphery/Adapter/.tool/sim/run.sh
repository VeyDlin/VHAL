#!/usr/bin/env bash
# Builds every host test of the adapter base classes and runs it with g++. Exits non-zero when a test fails to build or fails.
# Works in Git Bash on Windows and on Linux or macOS. Build output goes to a temporary directory outside the repository.
# The tests are optimised. A state the optimiser keeps in a register makes Await time out instead of seeing the interrupt's write,
# and TestAwaitEndedByAnotherThread fails.
set -u

simDir="$(cd "$(dirname "$0")" && pwd)"
vhalRoot="$(cd "$simDir/../../../.." && pwd)"
cxx="${CXX:-g++}"
buildDir="$(mktemp -d)"
trap 'rm -rf "$buildDir"' EXIT

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) exeSuffix=".exe" ;;
    *) exeSuffix="" ;;
esac

failures=0

for source in "$simDir"/Test*.cpp; do
    name="$(basename "$source" .cpp)"
    exe="$buildDir/$name$exeSuffix"

    if ! "$cxx" -std=c++23 -O2 -Wall -Wextra -Werror \
        -I "$simDir/shim" -I "$vhalRoot/Periphery" -I "$vhalRoot/Common" \
        -o "$exe" "$source" -pthread; then
        echo "BUILD FAILED: $name"
        failures=$((failures + 1))
        continue
    fi

    if ! "$exe"; then
        echo "TEST FAILED: $name"
        failures=$((failures + 1))
    fi
done

if [ "$failures" -ne 0 ]; then
    echo "$failures test executable(s) failed"
    exit 1
fi

echo "all test executables passed"
