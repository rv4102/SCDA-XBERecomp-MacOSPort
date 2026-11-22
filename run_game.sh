#!/bin/zsh
set -e

# Tom Clancy's Splinter Cell: Double Agent (Xbox Static Recompilation for Apple Silicon macOS)
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build-macos"
APP="${BUILD_DIR}/Splinter Cell Double Agent.app"

MODE="${1:-sp}"

if [[ "$MODE" == "versus" || "$MODE" == "mp" || "$MODE" == "multiplayer" ]]; then
    EXE="${APP}/Contents/MacOS/SCDA_Versus"
    echo "=== Launching Splinter Cell: Double Agent - Versus Multiplayer (arm64 Metal/Vulkan) ==="
else
    EXE="${APP}/Contents/MacOS/Splinter Cell Double Agent"
    echo "=== Launching Splinter Cell: Double Agent - Single Player / Co-op (arm64 Metal/Vulkan) ==="
fi

if [[ ! -f "$EXE" ]]; then
    echo "Game not built yet, building the app bundle..."
    ninja -C "$BUILD_DIR" SCDA
fi

exec "$EXE"
