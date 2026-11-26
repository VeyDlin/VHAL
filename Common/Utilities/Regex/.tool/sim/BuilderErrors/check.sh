#!/usr/bin/env bash
# Checks the regex parser's compile-time errors. Each snippet in this directory must fail to compile, and its diagnostic must contain
# the text on its first line, "// expect: <text>". A second line, "// expect-value: <number>", names the value of the error: the
# diagnostic must print it as the last template argument, ", <number>>". A snippet that compiles, or fails for another reason, fails
# the check. Exits non-zero when any snippet does not fail with its message.
set -u

dir="$(cd "$(dirname "$0")" && pwd)"
vhalRoot="$(cd "$dir/../../../../../.." && pwd)"
cxx="${CXX:-g++}"
failures=0

for snippet in "$dir"/*.cpp; do
    name="$(basename "$snippet")"
    firstLine=""
    secondLine=""
    { IFS= read -r firstLine; IFS= read -r secondLine; } < "$snippet"
    # A checkout with core.autocrlf gives the snippets CRLF line ends; the CR is not part of the expected text.
    firstLine="${firstLine%$'\r'}"
    secondLine="${secondLine%$'\r'}"
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

    expectedValue=""
    if [[ "$secondLine" == "// expect-value: "* ]]; then
        expectedValue="${secondLine#// expect-value: }"
    fi

    if output="$("$cxx" -std=c++23 -fsyntax-only -fno-exceptions -fno-rtti \
        -I "$vhalRoot/Common" \
        "$snippet" 2>&1)"; then
        echo "COMPILED, an error was expected: $name"
        failures=$((failures + 1))
        continue
    fi

    # GCC escapes the quotes and the backslashes of a template string argument (\' and \\). The text is read back unescaped.
    output="$(sed -e 's/\\\\/\\/g' -e "s/\\\\'/'/g" <<< "$output")"

    if ! grep -qF -- "$expected" <<< "$output"; then
        echo "WRONG DIAGNOSTIC: $name (expected text: $expected)"
        failures=$((failures + 1))
        continue
    fi

    # GCC prints the value as "Value = N]" in a function signature, and Clang as ", N>" in the template arguments.
    if [ -n "$expectedValue" ] && ! grep -qF -e "Value = $expectedValue]" -e ", $expectedValue>" <<< "$output"; then
        echo "WRONG VALUE: $name (expected value: $expectedValue)"
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
