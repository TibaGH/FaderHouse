#!/bin/bash
# Builds Faderhouse.app (including the "Mic FX" driver) without installing anything system-wide.
#
#   ./build.sh            Release build, opens the app when done
#   ./build.sh Debug      Debug build
#   ./build.sh --no-open  build only
#
# Needs only Apple's compiler (Xcode or the Command Line Tools). If CMake isn't on your
# system, a private copy is downloaded into ./.tools inside this folder (removable any time).

set -euo pipefail
cd "$(dirname "$0")"

CONFIG="Release"
OPEN_APP=1
for arg in "$@"; do
    case "$arg" in
        Debug|Release) CONFIG="$arg" ;;
        --no-open)     OPEN_APP=0 ;;
        *) echo "Unknown option: $arg"; exit 1 ;;
    esac
done

# --- 1. compiler ---------------------------------------------------------------
if ! xcode-select -p >/dev/null 2>&1 || ! xcrun --find clang++ >/dev/null 2>&1; then
    echo "Apple's compiler is missing. A macOS dialog will offer to install the Command Line Tools;"
    echo "run ./build.sh again when that is done."
    xcode-select --install || true
    exit 1
fi

# --- 2. CMake (private copy if needed) -----------------------------------------
CMAKE_VERSION="4.4.4"
CMAKE_SHA256="4b7b73704b1db9b374e5c9ab8e17ac6148b817b6396ee75cd94a852cbac9d305"
TOOLS_DIR="$PWD/.tools"
LOCAL_CMAKE="$TOOLS_DIR/cmake-$CMAKE_VERSION-macos-universal/CMake.app/Contents/bin/cmake"

if command -v cmake >/dev/null 2>&1; then
    CMAKE="$(command -v cmake)"
elif [ -x "$LOCAL_CMAKE" ]; then
    CMAKE="$LOCAL_CMAKE"
else
    echo "==> Downloading a private copy of CMake $CMAKE_VERSION into .tools (one time, ~90 MB)"
    mkdir -p "$TOOLS_DIR"
    ARCHIVE="$TOOLS_DIR/cmake.tar.gz"
    curl -fL --progress-bar -o "$ARCHIVE" \
        "https://github.com/Kitware/CMake/releases/download/v$CMAKE_VERSION/cmake-$CMAKE_VERSION-macos-universal.tar.gz"
    echo "$CMAKE_SHA256  $ARCHIVE" | shasum -a 256 -c - >/dev/null
    tar -xzf "$ARCHIVE" -C "$TOOLS_DIR"
    rm -f "$ARCHIVE"
    xattr -dr com.apple.quarantine "$TOOLS_DIR" 2>/dev/null || true
    CMAKE="$LOCAL_CMAKE"
fi

echo "==> Using $("$CMAKE" --version | head -1)"

# --- 3. configure + build ------------------------------------------------------
# The first run downloads JUCE and libASPL into build/_deps (a few minutes).
"$CMAKE" -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE="$CONFIG"
"$CMAKE" --build build --parallel "$(sysctl -n hw.ncpu)"

APP="build/Faderhouse_artefacts/$CONFIG/Faderhouse.app"
DRIVER="$APP/Contents/Resources/FaderhouseMicFX.driver"

if [ ! -d "$DRIVER" ]; then
    echo "Build finished but FaderhouseMicFX.driver is missing from the app."
    exit 1
fi

# --- 4. sign locally (ad-hoc, no Apple account needed) ------------------------------
codesign --force --sign - "$DRIVER"
codesign --force --sign - "$APP"

echo
echo "==> Done: $APP"
echo "    Move it to /Applications if you like; Faderhouse installs its virtual mic itself on first launch."

if [ "$OPEN_APP" = 1 ]; then
    open "$APP"
fi
