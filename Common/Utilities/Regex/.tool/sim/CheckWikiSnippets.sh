#!/usr/bin/env bash
# Compiles every ```cpp block of the regex wiki pages (.wiki/Common/Utilities/Regex/*.md) as its own translation unit, with the device
# flags and syntax only. A block must be complete on its own: its includes, its names and its static_asserts. Blocks that are not C++ use
# a ```text fence and are not compiled. Exits non-zero when a block does not compile, or when a page has no C++ block.
set -u

simDir="$(cd "$(dirname "$0")" && pwd)"
vhalRoot="$(cd "$simDir/../../../../.." && pwd)"
wikiDir="$vhalRoot/.wiki/Common/Utilities/Regex"
cxx="${CXX:-g++}"
buildDir="$(mktemp -d)"
trap 'rm -rf "$buildDir"' EXIT
failures=0
snippets=0

for page in "$wikiDir"/*.md; do
    pageName="$(basename "$page" .md)"
    # Each block goes to its own file, named after the page and its position. The awk prints the number of blocks it found.
    blockCount="$(awk -v dir="$buildDir" -v name="$pageName" '
        /^```cpp$/ { inBlock = 1; blockCount++; next }
        /^```$/ { inBlock = 0; next }
        inBlock { print > (dir "/" name "-" blockCount ".cpp") }
        END { print blockCount + 0 }
    ' "$page")"

    if [ "$blockCount" -eq 0 ]; then
        echo "NO C++ BLOCK: $pageName.md"
        failures=$((failures + 1))
        continue
    fi

    for ((index = 1; index <= blockCount; index++)); do
        snippet="$buildDir/$pageName-$index.cpp"
        snippets=$((snippets + 1))
        if ! "$cxx" -std=c++20 -fsyntax-only -Wall -Wextra -Werror -fno-exceptions -fno-rtti -I "$vhalRoot/Common" "$snippet"; then
            echo "SNIPPET FAILED: $pageName.md, C++ block $index"
            failures=$((failures + 1))
        fi
    done
done

if [ "$failures" -ne 0 ]; then
    echo "$failures wiki snippet check(s) failed"
    exit 1
fi

echo "all $snippets regex wiki snippets compile"
