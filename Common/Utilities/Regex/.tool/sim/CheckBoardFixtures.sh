#!/usr/bin/env bash
# Runs WriteBoardFixtures.cpp again and checks that its output is the committed fixtures/Board.inc, so the file is what its program writes.
# The program's output goes to a temporary directory outside the repository. Exits non-zero on a difference.
set -u

simDir="$(cd "$(dirname "$0")" && pwd)"
fixturesDir="$simDir/fixtures"
cxx="${CXX:-g++}"
buildDir="$(mktemp -d)"
trap 'rm -rf "$buildDir"' EXIT

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) exeSuffix=".exe" ;;
    *) exeSuffix="" ;;
esac

if ! "$cxx" -std=c++20 -Wall -Wextra -Werror -o "$buildDir/WriteBoardFixtures$exeSuffix" "$fixturesDir/WriteBoardFixtures.cpp"; then
    echo "BUILD FAILED: WriteBoardFixtures"
    exit 1
fi

if ! "$buildDir/WriteBoardFixtures$exeSuffix" > "$buildDir/Board.inc"; then
    echo "RUN FAILED: WriteBoardFixtures"
    exit 1
fi

if ! diff -u "$fixturesDir/Board.inc" "$buildDir/Board.inc"; then
    echo "BOARD FIXTURES DIFFER: fixtures/Board.inc is not the output of WriteBoardFixtures.cpp"
    exit 1
fi

echo "board fixtures match WriteBoardFixtures.cpp"
