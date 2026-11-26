#!/usr/bin/env bash
# Checks that the captures loop of the regex matcher is emitted only for a program that calls Find. NotEmitted.cpp is compiled twice to an
# object file: once with Matches only, which must have no symbol of RegexRunCaptures, and once with REGEX_USE_FIND, which must have one.
# The second part proves that nm can see the symbol. Exits non-zero when either part does not hold.
set -u

simDir="$(cd "$(dirname "$0")" && pwd)"
vhalRoot="$(cd "$simDir/../../../../.." && pwd)"
cxx="${CXX:-g++}"
buildDir="$(mktemp -d)"
trap 'rm -rf "$buildDir"' EXIT

# -O0 keeps the loop as its own function. At -O2 GCC inlines the one caller into Find, and the symbol would not show.
flags=(-std=c++23 -O0 -Wall -Wextra -Werror -fno-exceptions -fno-rtti -I "$vhalRoot/Common")

if ! "$cxx" "${flags[@]}" -c -o "$buildDir/matches.o" "$simDir/NotEmitted.cpp"; then
    echo "BUILD FAILED: NotEmitted.cpp (Matches only)"
    exit 1
fi

if ! "$cxx" "${flags[@]}" -DREGEX_USE_FIND -c -o "$buildDir/find.o" "$simDir/NotEmitted.cpp"; then
    echo "BUILD FAILED: NotEmitted.cpp (Find)"
    exit 1
fi

matchesSymbols="$(nm "$buildDir/matches.o")"
findSymbols="$(nm "$buildDir/find.o")"

if grep -q "RegexRunCaptures" <<< "$matchesSymbols"; then
    echo "FAILED: the Matches-only object has a symbol of RegexRunCaptures"
    exit 1
fi

if ! grep -q "RegexRunCaptures" <<< "$findSymbols"; then
    echo "FAILED: the Find object has no symbol of RegexRunCaptures, so the check cannot see it"
    exit 1
fi

echo "captures loop is emitted only with Find"
