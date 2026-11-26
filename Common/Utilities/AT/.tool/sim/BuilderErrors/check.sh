#!/usr/bin/env bash
# Checks the AT description's rules at compile time. Each snippet in this directory must fail to compile, and its diagnostic must
# contain the text on its first line, "// expect: <text>". A snippet that compiles, or fails for another reason, fails the check.
# Exits non-zero when any snippet does not fail with its message.
set -u

dir="$(cd "$(dirname "$0")" && pwd)"
vhalRoot="$(cd "$dir/../../../../../.." && pwd)"
cxx="${CXX:-g++}"
failures=0

for snippet in "$dir"/*.cpp; do
    name="$(basename "$snippet")"
    IFS= read -r firstLine < "$snippet"
    # A checkout with core.autocrlf gives the snippets CRLF line ends; the CR is not part of the expected text.
    firstLine="${firstLine%$'\r'}"
    if [[ "$firstLine" != "// expect: "* ]]; then
        echo "NO EXPECT HEADER: $name (the first line must be \"// expect: <text>\")"
        failures=$((failures + 1))
        continue
    fi

    expected="${firstLine#// expect: }"
    if [ -z "$expected" ]; then
        echo "EMPTY EXPECT TEXT: $name"
        failures=$((failures + 1))
        continue
    fi

    if output="$("$cxx" -std=c++23 -fsyntax-only \
        -I "$dir/../shim" -I "$vhalRoot/Periphery" -I "$vhalRoot/Common" \
        "$snippet" 2>&1)"; then
        echo "COMPILED, an error was expected: $name"
        failures=$((failures + 1))
        continue
    fi

    if ! grep -qF -- "$expected" <<< "$output"; then
        echo "WRONG DIAGNOSTIC: $name (expected text: $expected)"
        failures=$((failures + 1))
        continue
    fi

    echo "rejected as expected: $name"
done

if [ "$failures" -ne 0 ]; then
    echo "$failures builder error snippet(s) did not fail with their message"
    exit 1
fi

echo "all builder error snippets fail with their message"
