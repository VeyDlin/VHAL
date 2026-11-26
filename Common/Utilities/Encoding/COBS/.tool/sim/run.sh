#!/usr/bin/env bash
# Builds the COBS codec tests with g++, runs them, and checks that the header compiles alone. Exits non-zero when a test fails to build
# or fails. Works in Git Bash on Windows and on Linux or macOS. Build output goes to a temporary directory outside the repository.
#
# CXXFLAGS adds flags to the test builds, for example CXXFLAGS="-fsanitize=address,undefined" where the toolchain has the sanitizer
# libraries. The default run does not use sanitizers.
set -u

simDir="$(cd "$(dirname "$0")" && pwd)"
vhalRoot="$(cd "$simDir/../../../../../.." && pwd)"
cxx="${CXX:-g++}"
buildDir="$(mktemp -d)"
trap 'rm -rf "$buildDir"' EXIT

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) exeSuffix=".exe" ;;
    *) exeSuffix="" ;;
esac

failures=0
commonFlags=(-std=c++23 -Wall -Wextra -Werror -I "$vhalRoot/Common")

headerSource="$buildDir/HeaderAlone.cpp"
printf '%s\n' '#include <Utilities/Encoding/COBS/Cobs.h>' > "$headerSource"
if ! "$cxx" "${commonFlags[@]}" ${CXXFLAGS:-} -fsyntax-only "$headerSource"; then
    echo "HEADER FAILED: Cobs.h does not compile alone"
    failures=$((failures + 1))
fi

for source in "$simDir"/Test*.cpp; do
    name="$(basename "$source" .cpp)"
    exe="$buildDir/$name$exeSuffix"

    if ! "$cxx" "${commonFlags[@]}" ${CXXFLAGS:-} -o "$exe" "$source"; then
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
    echo "$failures step(s) failed"
    exit 1
fi

echo "All COBS checks passed"
exit 0
