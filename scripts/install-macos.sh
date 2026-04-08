#!/usr/bin/env bash
# Install obs-multi-rtmp.plugin into OBS on macOS.
#
# Usage:
#   ./install-macos.sh                              # auto-finds zip/plugin in ~/Downloads
#   ./install-macos.sh path/to/obs-multi-rtmp.zip
#   ./install-macos.sh path/to/obs-multi-rtmp.plugin
set -euo pipefail

PLUGIN_NAME="obs-multi-rtmp-aa.plugin"
PLUGIN_DIR="$HOME/Library/Application Support/obs-studio/plugins"
SOURCE="${1:-}"

say()  { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m!!\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31mxx\033[0m %s\n' "$*" >&2; exit 1; }

# ---- 1. Locate source ------------------------------------------------------
if [[ -z "$SOURCE" ]]; then
  say "No path given — searching ~/Downloads"
  CANDIDATE=$(find "$HOME/Downloads" -maxdepth 2 \
                \( -name 'obs-multi-rtmp*.plugin' -o -name 'obs-multi-rtmp*.zip' \) \
                -print 2>/dev/null | head -n1)
  [[ -n "$CANDIDATE" ]] || die "No obs-multi-rtmp zip or .plugin found in ~/Downloads. Pass a path explicitly."
  SOURCE="$CANDIDATE"
fi
[[ -e "$SOURCE" ]] || die "Not found: $SOURCE"
say "Source: $SOURCE"

# ---- 2. Extract if zip -----------------------------------------------------
TMP=""
cleanup() { [[ -n "$TMP" && -d "$TMP" ]] && rm -rf "$TMP"; }
trap cleanup EXIT

# The CI artifact is a zip that contains a tar.xz that contains the .plugin
# bundle. Handle any combination: zip, tar.xz, or already-extracted bundle.
extract_archive() {
  local src="$1" dst="$2"
  case "$src" in
    *.zip)               unzip -q "$src" -d "$dst" ;;
    *.tar.xz|*.txz)      tar -xJf "$src" -C "$dst" ;;
    *.tar.gz|*.tgz)      tar -xzf "$src" -C "$dst" ;;
    *.tar)               tar -xf  "$src" -C "$dst" ;;
    *) return 1 ;;
  esac
}

if [[ -d "$SOURCE" && "$SOURCE" == *.plugin ]]; then
  BUNDLE="$SOURCE"
else
  TMP=$(mktemp -d)
  say "Extracting $(basename "$SOURCE")"
  extract_archive "$SOURCE" "$TMP" || die "Unrecognized source: $SOURCE"

  # If the archive contained another archive (zip → tar.xz), unwrap it.
  for _ in 1 2; do
    BUNDLE=$(find "$TMP" -maxdepth 4 -type d -name "$PLUGIN_NAME" -print -quit)
    [[ -n "$BUNDLE" ]] && break
    NESTED=$(find "$TMP" -maxdepth 2 -type f \
               \( -name '*.tar.xz' -o -name '*.txz' -o -name '*.tar.gz' \
                  -o -name '*.tgz'   -o -name '*.tar' -o -name '*.zip' \) \
               -print -quit)
    [[ -n "$NESTED" ]] || break
    say "Unwrapping nested $(basename "$NESTED")"
    extract_archive "$NESTED" "$TMP" || die "Failed to extract $NESTED"
    rm -f "$NESTED"
  done

  [[ -n "$BUNDLE" ]] || die "Could not find $PLUGIN_NAME inside $SOURCE"
fi

# ---- 3. Verify it's a real plugin bundle -----------------------------------
# The binary name inside the .plugin tracks the CMake target name, which
# matches the bundle basename minus ".plugin". Don't hardcode it — discover
# the first Mach-O under Contents/MacOS/ so this keeps working if we rename.
BIN=$(find "$BUNDLE/Contents/MacOS" -maxdepth 1 -type f -perm -u+x -print -quit 2>/dev/null || true)
[[ -n "$BIN" && -f "$BIN" ]] || die "Bundle $BUNDLE has no executable under Contents/MacOS — not a valid OBS plugin"
say "Binary: $(basename "$BIN")"
say "Architectures: $(lipo -archs "$BIN" 2>/dev/null || file "$BIN")"

# ---- 4. Quit OBS if it's running -------------------------------------------
if pgrep -x OBS >/dev/null; then
  warn "OBS is running — quitting it (save your work first!)"
  read -r -p "Press enter to quit OBS, or Ctrl-C to abort: " _
  osascript -e 'tell application "OBS" to quit' 2>/dev/null || true
  # Wait up to 5s for clean exit
  for _ in 1 2 3 4 5; do pgrep -x OBS >/dev/null || break; sleep 1; done
  pgrep -x OBS >/dev/null && die "OBS didn't quit — close it manually and rerun."
fi

# ---- 5. Install ------------------------------------------------------------
mkdir -p "$PLUGIN_DIR"
DEST="$PLUGIN_DIR/$PLUGIN_NAME"
if [[ -e "$DEST" ]]; then
  say "Removing previous install at $DEST"
  rm -rf "$DEST"
fi
say "Copying bundle → $DEST"
cp -R "$BUNDLE" "$DEST"

# ---- 6. Strip quarantine (critical — OBS silently rejects quarantined bundles)
say "Clearing com.apple.quarantine xattr"
xattr -dr com.apple.quarantine "$DEST" 2>/dev/null || true

# ---- 7. Done ---------------------------------------------------------------
say "Installed ✓"
echo
echo "Next:"
echo "  1. Open OBS"
echo "  2. Docks menu → enable 'Multiple output AA' (obs-multi-rtmp-aa-dock)"
echo "  3. If the dock doesn't appear, check Help → Log Files → View Current Log"
echo "     and grep for 'obs-multi-rtmp-aa'"
