#!/usr/bin/env bash
# Builds and runs every host check of the regex matcher with g++ (CXX overrides the compiler). The Test*.cpp files are built with the
# device flags and run again with address and undefined-behaviour sanitizers, when the toolchain has their runtimes; otherwise that
# loop is skipped and the output says so. The Compare*.cpp files are built with exceptions and RTTI,
# which std::regex needs; the generated patterns are split into four translation units that build in parallel. Then CheckNotEmitted.sh,
# the compile-time error snippets of BuilderErrors/, the board fixtures (fixtures/) and the C++ blocks of the wiki pages run. Build output
# goes to a temporary directory outside the repository.
# Exits non-zero when a check fails. Works in Git Bash on Windows and on Linux or macOS.
set -u

simDir="$(cd "$(dirname "$0")" && pwd)"
vhalRoot="$(cd "$simDir/../../../../.." && pwd)"
cxx="${CXX:-g++}"
buildDir="$(mktemp -d)"
trap 'rm -rf "$buildDir"' EXIT
generatedParts=4

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) exeSuffix=".exe" ;;
    *) exeSuffix="" ;;
esac

deviceFlags=(-std=c++20 -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
hostFlags=(-std=c++20 -Wall -Wextra -Werror)
includes=(-I "$vhalRoot/Common")
failures=0

# Builds one Test file with the given flags, then runs it. $1 is the source, $2 the name of the executable, the rest are the flags.
RunTest() {
    local source="$1"
    local name="$2"
    shift 2
    local exe="$buildDir/$name$exeSuffix"
    if ! "$cxx" "$@" "${includes[@]}" -o "$exe" "$source"; then
        echo "BUILD FAILED: $name"
        return 1
    fi

    if ! "$exe"; then
        echo "TEST FAILED: $name"
        return 1
    fi

    return 0
}

for source in "$simDir"/Test*.cpp; do
    name="$(basename "$source" .cpp)"
    if ! RunTest "$source" "$name" "${deviceFlags[@]}"; then
        failures=$((failures + 1))
    fi
done

# A toolchain without the sanitizer runtimes (MinGW without libasan and libubsan) fails to link even a trivial program with them. The loop
# runs only when such a program links and runs, so a real sanitizer failure is still a failed check.
printf 'int main() { return 0; }\n' > "$buildDir/SanitizerProbe.cpp"
sanitizers=0
if "$cxx" -fsanitize=address,undefined -o "$buildDir/SanitizerProbe$exeSuffix" "$buildDir/SanitizerProbe.cpp" > /dev/null 2>&1 \
    && "$buildDir/SanitizerProbe$exeSuffix" > /dev/null 2>&1; then
    sanitizers=1
else
    echo "SKIPPED: sanitizers unavailable (the toolchain cannot link and run -fsanitize=address,undefined)"
fi

if [ "$sanitizers" -eq 1 ]; then
    for source in "$simDir"/Test*.cpp; do
        name="$(basename "$source" .cpp)"
        if ! RunTest "$source" "$name-sanitized" "${deviceFlags[@]}" -g -fsanitize=address,undefined -fno-sanitize-recover=undefined; then
            failures=$((failures + 1))
        fi
    done
fi

if ! RunTest "$simDir/CompareSyntax.cpp" "CompareSyntax" "${hostFlags[@]}"; then
    failures=$((failures + 1))
fi

if ! "$cxx" "${hostFlags[@]}" -o "$buildDir/GenerateCases$exeSuffix" "$simDir/GenerateCases.cpp"; then
    echo "BUILD FAILED: GenerateCases"
    failures=$((failures + 1))
fi

# The four parts of the generated patterns build and run in parallel, each with its own log, and the logs are printed in order.
comparePart() {
    local part="$1"
    local log="$buildDir/comparePart$part.log"
    {
        "$buildDir/GenerateCases$exeSuffix" "$part" "$generatedParts" > "$buildDir/GeneratedCases$part.inc" || exit 1
        "$cxx" "${hostFlags[@]}" "${includes[@]}" "-DREGEX_CASES_FILE=\"GeneratedCases$part.inc\"" -I "$buildDir" \
            -o "$buildDir/CompareGenerated$part$exeSuffix" "$simDir/CompareGenerated.cpp" || exit 1
        "$buildDir/CompareGenerated$part$exeSuffix" || exit 1
    } > "$log" 2>&1
}

partPids=()
for ((part = 0; part < generatedParts; part++)); do
    comparePart "$part" &
    partPids+=($!)
done

for ((part = 0; part < generatedParts; part++)); do
    if ! wait "${partPids[$part]}"; then
        echo "CHECK FAILED: generated patterns, part $part"
        failures=$((failures + 1))
    fi

    cat "$buildDir/comparePart$part.log"
done

if ! bash "$simDir/CheckNotEmitted.sh"; then
    failures=$((failures + 1))
fi

if ! "$simDir/BuilderErrors/check.sh"; then
    failures=$((failures + 1))
fi

if ! bash "$simDir/CheckBoardFixtures.sh"; then
    failures=$((failures + 1))
fi

if ! bash "$simDir/CheckWikiSnippets.sh"; then
    failures=$((failures + 1))
fi

if [ "$failures" -ne 0 ]; then
    echo "$failures check(s) failed"
    exit 1
fi

if [ "$sanitizers" -eq 1 ]; then
    echo "all regex checks passed"
else
    echo "all regex checks passed (sanitizers skipped)"
fi
