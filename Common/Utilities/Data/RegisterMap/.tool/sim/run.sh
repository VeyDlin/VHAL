#!/usr/bin/env bash
# Builds and runs the host tests of the RegisterMap descriptors, bus, access functions, set and view. Also checks that every
# header compiles alone, that the builder error snippets fail with their messages, and that an unused descriptor leaves no
# symbol at -Os. Works in Git Bash on Windows and on Linux or macOS. Build output goes to a temporary directory outside the repository.
set -u

simDir="$(cd "$(dirname "$0")" && pwd)"
vhalRoot="$(cd "$simDir/../../../../../.." && pwd)"
regMapDir="$vhalRoot/Common/Utilities/Data/RegisterMap"
cxx="${CXX:-g++}"
buildDir="$(mktemp -d)"
trap 'rm -rf "$buildDir"' EXIT

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) exeSuffix=".exe" ;;
    *) exeSuffix="" ;;
esac

flags=(-std=c++23 -Wall -Wextra -Werror -Wconversion -I "$vhalRoot/Common" -I "$simDir")
failures=0

for source in "$simDir"/Test*.cpp; do
    name="$(basename "$source" .cpp)"
    exe="$buildDir/$name$exeSuffix"

    if ! "$cxx" "${flags[@]}" -o "$exe" "$source"; then
        echo "BUILD FAILED: $name"
        failures=$((failures + 1))
        continue
    fi

    if ! "$exe"; then
        echo "TEST FAILED: $name"
        failures=$((failures + 1))
    fi
done

for header in "$regMapDir"/*.h; do
    name="$(basename "$header")"
    probe="$buildDir/header_$name.cpp"
    printf '#include <Utilities/Data/RegisterMap/%s>\n' "$name" > "$probe"
    if ! "$cxx" "${flags[@]}" -fsyntax-only "$probe"; then
        echo "HEADER DOES NOT COMPILE ALONE: $name"
        failures=$((failures + 1))
    fi
done

if ! bash "$simDir/BuilderErrors/check.sh"; then
    failures=$((failures + 1))
fi

unusedObject="$buildDir/UnusedDescriptor.o"
if ! "$cxx" -std=c++23 -Os -Wall -Wextra -Werror -I "$vhalRoot/Common" -I "$simDir" -c -o "$unusedObject" "$simDir/UnusedDescriptor.cpp"; then
    echo "UNUSED DESCRIPTOR PROBE DOES NOT BUILD"
    failures=$((failures + 1))
elif nm -C "$unusedObject" | grep -q "UnusedProbeDescriptor"; then
    echo "UNUSED DESCRIPTOR LEFT A SYMBOL AT -Os"
    failures=$((failures + 1))
fi

if [ "$failures" -ne 0 ]; then
    echo "$failures RegisterMap check(s) failed"
    exit 1
fi

echo "all RegisterMap checks passed"
