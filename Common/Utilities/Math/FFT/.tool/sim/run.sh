#!/usr/bin/env bash
# Builds every host test and demo of the FFT and its sample sources and runs them with g++, then checks the compile-error
# snippets in BuilderErrors. Exits non-zero when a test fails to build or fails, or when a snippet does not fail with its message.
# Works in Git Bash on Windows and on Linux or macOS. Build output goes to a temporary directory outside the repository.
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

for source in "$simDir"/Test*.cpp "$simDir"/Demo*.cpp; do
    name="$(basename "$source" .cpp)"
    exe="$buildDir/$name$exeSuffix"

    if ! "$cxx" -std=c++23 -Wall -Wextra -Werror -pthread \
        -I "$vhalRoot/Common" \
        -o "$exe" "$source"; then
        echo "BUILD FAILED: $name"
        failures=$((failures + 1))
        continue
    fi

    if ! "$exe"; then
        echo "TEST FAILED: $name"
        failures=$((failures + 1))
    fi
done

if ! bash "$simDir/BuilderErrors/check.sh"; then
    failures=$((failures + 1))
fi

if [ "$failures" -ne 0 ]; then
    echo "$failures test executable(s) or check group(s) failed"
    exit 1
fi

echo "all FFT tests passed"
