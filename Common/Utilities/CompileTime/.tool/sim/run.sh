#!/usr/bin/env bash
# Builds every host test of the compile-time checks with g++ and runs it, then checks the builder error snippets. Exits non-zero
# when a test fails to build or fails. Works in Git Bash on Windows and on Linux or macOS. Build output goes to a temporary
# directory outside the repository.
set -u

simDir="$(cd "$(dirname "$0")" && pwd)"
vhalRoot="$(cd "$simDir/../../../../.." && pwd)"
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

    if ! "$cxx" -std=c++20 -Wall -Wextra -Werror \
        -I "$vhalRoot/Common" -I "$simDir" \
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

# The aborting path of the host definitions: a check that fails at run time prints its message and stops the program.
for mode in plain value; do
    name="RuntimeFailure_$mode"
    exe="$buildDir/$name$exeSuffix"

    if [ "$mode" = "plain" ]; then
        expected="CompileTime: run-time failure"
    else
        expected="CompileTime: value failure (value 7)"
    fi

    if ! "$cxx" -std=c++20 -Wall -Wextra -Werror \
        -I "$vhalRoot/Common" -I "$simDir" \
        -o "$exe" "$simDir/RuntimeFailures/RequireRuntimeFailure.cpp"; then
        echo "BUILD FAILED: $name"
        failures=$((failures + 1))
        continue
    fi

    output="$("$exe" "$mode" 2>&1)"
    status=$?
    if [ "$status" -eq 0 ]; then
        echo "RUN-TIME FAILURE DID NOT STOP THE PROGRAM: $name"
        failures=$((failures + 1))
    elif ! grep -qF -- "$expected" <<< "$output"; then
        echo "RUN-TIME FAILURE MESSAGE MISSING: $name (expected text: $expected)"
        failures=$((failures + 1))
    fi
done

if ! bash "$simDir/BuilderErrors/check.sh"; then
    echo "BUILDER ERROR CHECK FAILED"
    failures=$((failures + 1))
fi

if [ "$failures" -ne 0 ]; then
    echo "$failures test executable(s) failed"
    exit 1
fi

echo "all test executables passed"
