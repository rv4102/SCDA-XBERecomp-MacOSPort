#!/bin/bash
# Makes the story build a self-contained macOS application bundle:
#   - the game image ships in Contents/Resources/assets, where the game looks for it
#   - the versus executable rides along in Contents/MacOS and gets a launcher
#   - the Homebrew Vulkan/MoltenVK/glslang/spirv-tools libraries are copied into Contents/Frameworks and every
#     absolute /opt/homebrew (or /usr/local) reference is rewritten to @rpath, so the bundle also runs on Macs
#     that never installed those formulas
#
# usage: bundle_macos.sh <app bundle> <main exe> <versus exe> <assets dir> <icon> <info.plist>
set -eo pipefail

APP="$1"
EXE="$2"
VERSUS="$3"
ASSETS="$4"
ICON="$5"
PLIST="$6"

HERE="$(cd "$(dirname "$0")" && pwd)"
FW="$APP/Contents/Frameworks"
MACOS="$APP/Contents/MacOS"
RES="$APP/Contents/Resources"

[ -d "$APP" ] || { echo "bundle_macos: no bundle at $APP" >&2; exit 1; }
mkdir -p "$FW" "$RES/assets" "$MACOS"

# CMake only writes the Info.plist when it generates the build tree, so the bundle deleted and rebuilt by hand would
# come back without one (no bundle id, no icon, no app registration); the packaging step owns it here
cp -f "$PLIST" "$APP/Contents/Info.plist"

# The game data: clone when the filesystem can (APFS), which costs no extra space; copy otherwise.
copy_asset() {
    local src="$1" dst="$2"
    [ -f "$src" ] || return 0
    if [ -f "$dst" ] && [ "$(stat -f %z "$src")" = "$(stat -f %z "$dst")" ] &&
       [ "$(stat -f %m "$dst")" -ge "$(stat -f %m "$src")" ]; then
        return 0
    fi
    rm -f "$dst"
    cp -c "$src" "$dst" 2>/dev/null || cp "$src" "$dst"
}
copy_asset "$ASSETS/game.iso" "$RES/assets/game.iso"

# the versus build shares the same game image; it is launched from inside the bundle
cp -f "$VERSUS" "$MACOS/SCDA_Versus"

[ -f "$ICON" ] && cp -f "$ICON" "$RES/AppIcon.icns"

# the loader reads this manifest instead of Homebrew's, so it loads the MoltenVK inside the bundle
mkdir -p "$RES/vulkan/icd.d"
cp -f "$HERE/MoltenVK_icd.json" "$RES/vulkan/icd.d/MoltenVK_icd.json"

# every Homebrew library the two executables need, transitively
deps_of() {
    otool -L "$1" 2>/dev/null | tail -n +2 |
        sed -n 's/^[[:space:]]*\([^ ]*\) (compatibility.*/\1/p' |
        grep -E '^/(opt/homebrew|usr/local)/' || true
}

LIBS=""
walk() {
    local bin="$1" dep
    while IFS= read -r dep; do
        [ -n "$dep" ] || continue
        case ":$LIBS:" in
            *":$dep:"*) continue ;;
        esac
        LIBS="$LIBS:$dep"
        walk "$dep"
    done < <(deps_of "$bin")
}
walk "$EXE"
walk "$VERSUS"

IFS=':' read -r -a LIB_ARRAY <<< "${LIBS#:}"
if [ "${#LIB_ARRAY[@]}" -eq 0 ]; then
    echo "bundle_macos: warning: the executables link no Homebrew libraries, nothing to bundle" >&2
fi

for lib in "${LIB_ARRAY[@]}"; do
    [ -n "$lib" ] || continue
    base="$(basename "$lib")"
    cp -f "$lib" "$FW/$base"
    chmod 755 "$FW/$base"
    install_name_tool -id "@rpath/$base" "$FW/$base"
done

# point this binary's Homebrew dependencies at the bundled copies and give it a path to them
rewrite_binary() {
    local bin="$1" dep base rp
    while IFS= read -r dep; do
        [ -n "$dep" ] || continue
        base="$(basename "$dep")"
        [ -f "$FW/$base" ] && install_name_tool -change "$dep" "@rpath/$base" "$bin"
    done < <(deps_of "$bin")
    if ! otool -l "$bin" | grep -qF "@executable_path/../Frameworks"; then
        install_name_tool -add_rpath "@executable_path/../Frameworks" "$bin"
    fi
    # CMake also embeds an /opt/homebrew/lib rpath; drop it, or @rpath finds Homebrew's copy before the bundle's
    for rp in $(otool -l "$bin" 2>/dev/null | sed -n '/LC_RPATH/{n;n;s/^ *path //;s/ (offset.*//;p;}'); do
        case "$rp" in
            /opt/*|/usr/local/*) install_name_tool -delete_rpath "$rp" "$bin" ;;
        esac
    done
}
rewrite_binary "$EXE"
rewrite_binary "$MACOS/SCDA_Versus"
for lib in "${LIB_ARRAY[@]}"; do
    [ -n "$lib" ] || continue
    rewrite_binary "$FW/$(basename "$lib")"
done

# nothing may still point outside the bundle (a stray one only works on this Mac)
LEFTOVERS="$( { deps_of "$EXE"; deps_of "$MACOS/SCDA_Versus"; for lib in "${LIB_ARRAY[@]}"; do deps_of "$FW/$(basename "$lib")"; done; } | sort -u )"
if [ -n "$LEFTOVERS" ]; then
    echo "bundle_macos: warning: still linked outside the bundle:" >&2
    printf '  %s\n' "$LEFTOVERS" >&2
fi

# a launcher for the versus build, inside the bundle and beside it
make_launcher() {
    local dst="$1"
    cat > "$dst" <<'LAUNCH'
#!/bin/zsh
# Starts the multiplayer (versus) build that ships inside Splinter Cell Double Agent.app
set -e
here="$(cd "$(dirname "$0")" && pwd)"
case "$here" in
    */Contents/Resources) app="$(dirname "$(dirname "$here")")" ;;
    *) app="$here/Splinter Cell Double Agent.app" ;;
esac
exec "$app/Contents/MacOS/SCDA_Versus"
LAUNCH
    chmod +x "$dst"
}
make_launcher "$RES/Play Versus (Multiplayer).command"
make_launcher "$(dirname "$APP")/Play Versus (Multiplayer).command"

# modifying Mach-O files invalidates the linker's ad-hoc signature; --deep re-signs every binary inside and seals
# the bundle last, so the recorded hashes match the rewritten files
codesign --force --deep --sign - "$APP" >/dev/null 2>&1 ||
    echo "bundle_macos: warning: could not sign $APP" >&2

echo "bundle_macos: packaged $(basename "$APP") ($(du -sh "$APP" | cut -f1))"
