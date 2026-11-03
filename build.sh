#!/bin/sh
# Builds Packix.app, bundles Qt into it, signs it locally and installs it into /Applications.
set -e
cd "$(dirname "$0")"

cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build build-release --target Packix

rm -rf dist
mkdir -p dist
cp -R build-release/Packix.app dist/
/opt/homebrew/opt/qtbase/bin/macdeployqt dist/Packix.app -always-overwrite
# The SVG icon engine is not used and its QtSvg dependency lives outside qtbase.
rm -rf dist/Packix.app/Contents/PlugIns/iconengines

codesign --force --deep --sign - dist/Packix.app
# Quit a running copy so the new one starts fresh.
osascript -e 'tell application "Packix" to quit' >/dev/null 2>&1 || true
pkill -x Packix 2>/dev/null || true
rm -rf /Applications/Packix.app
cp -R dist/Packix.app /Applications/
xattr -dr com.apple.quarantine /Applications/Packix.app 2>/dev/null || true
# Tell Launch Services and the Services menu about the new copy.
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f /Applications/Packix.app
/System/Library/CoreServices/pbs -update 2>/dev/null || true
echo "Installed /Applications/Packix.app"

VERSION=$(sed -n 's/^project(Packix VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
rm -rf dist/dmg "dist/Packix-$VERSION.dmg"
mkdir -p dist/dmg
cp -R dist/Packix.app dist/dmg/
ln -s /Applications dist/dmg/Applications
hdiutil create -quiet -volname "Packix $VERSION" -srcfolder dist/dmg -ov -format UDZO "dist/Packix-$VERSION.dmg"
rm -rf dist/dmg
echo "Disk image dist/Packix-$VERSION.dmg"
