#!/bin/bash
# Makes the installers from an existing Release build (run ./build.sh --no-open first).
#
#   dist/Faderhouse-<version>.pkg   installs Faderhouse.app into /Applications and the
#                                   Mic FX device in one go (one password prompt)
#   dist/Faderhouse-<version>.dmg   classic "drag Faderhouse to Applications" window;
#                                   the app offers to install Mic FX on first launch
#
# Everything used here ships with macOS, except dmgbuild (pip) for the nicely laid out
# DMG window; without it a plain DMG is made.

set -euo pipefail
cd "$(dirname "$0")"

APP="build/Faderhouse_artefacts/Release/Faderhouse.app"
DRIVER="$APP/Contents/Resources/FaderhouseMicFX.driver"
[ -d "$APP" ] || { echo "Build first: ./build.sh --no-open"; exit 1; }
[ -d "$DRIVER" ] || { echo "Mic FX driver missing from the app bundle"; exit 1; }

VERSION=$(/usr/libexec/PlistBuddy -c "Print CFBundleShortVersionString" "$APP/Contents/Info.plist")
DIST="$PWD/dist"
WORK="$PWD/build/package"
rm -rf "$WORK" && mkdir -p "$WORK" "$DIST"

echo "==> Faderhouse $VERSION"

# --- .pkg ---------------------------------------------------------------------------
ROOT="$WORK/root"
mkdir -p "$ROOT/Applications" "$ROOT/Library/Audio/Plug-Ins/HAL"
ditto "$APP" "$ROOT/Applications/Faderhouse.app"
ditto "$DRIVER" "$ROOT/Library/Audio/Plug-Ins/HAL/FaderhouseMicFX.driver"

mkdir -p "$WORK/scripts"
cat > "$WORK/scripts/postinstall" <<'EOF'
#!/bin/bash
# Remove the driver of early test builds and let Core Audio load Mic FX.
rm -rf "/Library/Audio/Plug-Ins/HAL/MixwerkMic.driver"
killall coreaudiod 2>/dev/null || launchctl kickstart -k system/com.apple.audio.coreaudiod 2>/dev/null || true

# Open Faderhouse again for the logged-in user (handy after an in-app update).
CONSOLE_USER=$(stat -f%Su /dev/console 2>/dev/null || true)
if [ -n "$CONSOLE_USER" ] && [ "$CONSOLE_USER" != "root" ]; then
    sleep 2
    launchctl asuser "$(id -u "$CONSOLE_USER")" sudo -u "$CONSOLE_USER" open -a "/Applications/Faderhouse.app" 2>/dev/null || true
fi
exit 0
EOF
chmod +x "$WORK/scripts/postinstall"

# Always install into /Applications (don't "relocate" to some other copy of the app).
pkgbuild --analyze --root "$ROOT" "$WORK/components.plist"
for i in $(seq 0 10); do
    /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$WORK/components.plist" 2>/dev/null || true
done

pkgbuild --root "$ROOT" \
         --component-plist "$WORK/components.plist" \
         --scripts "$WORK/scripts" \
         --identifier com.baloghproductions.faderhouse.pkg \
         --version "$VERSION" \
         --install-location / \
         "$WORK/Faderhouse-component.pkg"

mkdir -p "$WORK/resources"
cp Resources/dmg-background.png "$WORK/resources/background.png"
cat > "$WORK/resources/welcome.html" <<EOF
<html><body style="font-family: -apple-system, Helvetica; font-size: 13px;">
<h2>Faderhouse $VERSION</h2>
<p>This installs:</p>
<ul>
<li><b>Faderhouse</b> in your Applications folder</li>
<li><b>Mic FX</b>, a virtual microphone: choose it as input in Discord / OBS to get your mic with its plugins</li>
</ul>
<p>The macOS audio system restarts once at the end, so sound drops for a moment.</p>
</body></html>
EOF

cat > "$WORK/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>Faderhouse $VERSION</title>
    <welcome file="welcome.html" mime-type="text/html"/>
    <background file="background.png" alignment="bottomleft" scaling="none" mime-type="image/png"/>
    <options customize="never" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_localSystem="true"/>
    <os-version min="14.2"/>
    <choices-outline><line choice="default"/></choices-outline>
    <choice id="default" title="Faderhouse"><pkg-ref id="com.baloghproductions.faderhouse.pkg"/></choice>
    <pkg-ref id="com.baloghproductions.faderhouse.pkg" version="$VERSION" onConclusion="none">Faderhouse-component.pkg</pkg-ref>
</installer-gui-script>
EOF

productbuild --distribution "$WORK/distribution.xml" \
             --resources "$WORK/resources" \
             --package-path "$WORK" \
             "$DIST/Faderhouse-$VERSION.pkg"

# --- .dmg ---------------------------------------------------------------------------
DMG="$DIST/Faderhouse-$VERSION.dmg"
rm -f "$DMG"

if command -v dmgbuild >/dev/null 2>&1 || python3 -c "import dmgbuild" 2>/dev/null; then
    tiffutil -cathidpicheck Resources/dmg-background.png Resources/dmg-background@2x.png -out "$WORK/background.tiff"
    cat > "$WORK/dmg-settings.py" <<'EOF'
import os.path
app = defines["app"]
files = [app]
symlinks = {"Applications": "/Applications"}
icon_locations = {os.path.basename(app): (160, 190), "Applications": (480, 190)}
background = defines["background"]
window_rect = ((200, 140), (640, 400))
icon_size = 112
text_size = 13
show_status_bar = False
show_tab_view = False
show_toolbar = False
show_pathbar = False
show_sidebar = False
format = "UDZO"
EOF
    python3 -m dmgbuild -s "$WORK/dmg-settings.py" -D app="$APP" -D background="$WORK/background.tiff" "Faderhouse" "$DMG"
else
    echo "(dmgbuild not found: making a plain DMG; pip3 install dmgbuild for the drag window)"
    mkdir -p "$WORK/dmg"
    ditto "$APP" "$WORK/dmg/Faderhouse.app"
    ln -s /Applications "$WORK/dmg/Applications"
    hdiutil create -volname "Faderhouse" -srcfolder "$WORK/dmg" -ov -format UDZO "$DMG"
fi

# --- checksums (the in-app updater verifies the .pkg against this) -----------------------
(cd "$DIST" && shasum -a 256 "Faderhouse-$VERSION.pkg" "Faderhouse-$VERSION.dmg" > SHA256SUMS.txt)

echo
echo "==> Installers in dist/:"
ls -lh "$DIST"
