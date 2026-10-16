#!/bin/bash
#
# DiVERE-specific macOS release builder.
# It deliberately does not use packaging/macosx/*hb*.sh.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
ARCH="$(uname -m)"
VERSION="5.6.1"
DISPLAY_VERSION="5.6.1-银盐菲林日记"
BUILD_DIR="$SRC_DIR/build/divere-$ARCH"
INSTALL_DIR="$BUILD_DIR/install"
STAGE_DIR="$BUILD_DIR/stage"
APP_DIR="$STAGE_DIR/darktable.app"
CONTENTS_DIR="$APP_DIR/Contents"
MACOS_DIR="$CONTENTS_DIR/MacOS"
RESOURCES_DIR="$CONTENTS_DIR/Resources"
OUT_DIR="$BUILD_DIR/out"
DMG_NAME="darktable-DiVERE-${VERSION}-${ARCH}.dmg"
JOBS="$(sysctl -n hw.ncpu)"
HB_PREFIX="$(brew --prefix)"

echo "==> Cleaning previous DiVERE build"
rm -rf "$BUILD_DIR"
mkdir -p "$INSTALL_DIR" "$STAGE_DIR" "$OUT_DIR"

echo "==> Configuring darktable DiVERE for $ARCH"
cmake -S "$SRC_DIR" -B "$BUILD_DIR/cmake" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.5 \
  -DCMAKE_PREFIX_PATH="$HB_PREFIX" \
  -DBINARY_PACKAGE_BUILD=ON \
  -DRAWSPEED_ENABLE_LTO=ON \
  -DUSE_AI=ON \
  -DUSE_OPENCL=ON \
  -DUSE_OPENMP=ON \
  -DUSE_LUA=ON \
  -DUSE_CAMERA_SUPPORT=ON \
  -DUSE_GRAPHICSMAGICK=ON \
  -DUSE_IMAGEMAGICK=OFF \
  -DUSE_GMIC=ON \
  -DUSE_MAP=ON \
  -DUSE_SDL2=ON \
  -DUSE_PORTMIDI=ON \
  -DUSE_JXL=ON \
  -DUSE_AVIF=ON \
  -DUSE_HEIF=ON \
  -DUSE_OPENEXR=ON \
  -DUSE_OPENJPEG=ON \
  -DUSE_WEBP=ON \
  -DUSE_XCF=ON \
  -DUSE_COLORD=OFF \
  -DUSE_KWALLET=OFF \
  -DBUILD_PRINT=ON \
  -DBUILD_CMSTEST=OFF \
  -DBUILD_CURVE_TOOLS=ON \
  -DBUILD_NOISE_TOOLS=ON \
  -DTESTBUILD_OPENCL_PROGRAMS=OFF \
  -DPROJECT_VERSION="$VERSION" \
  -DDT_VERSION_STRING="$DISPLAY_VERSION"

assert_features() {
  local feature
  for feature in USE_AI USE_OPENCL USE_CAMERA_SUPPORT USE_GRAPHICSMAGICK USE_GMIC USE_LUA USE_SDL2 USE_PORTMIDI USE_AVIF USE_HEIF USE_OPENEXR USE_OPENJPEG USE_WEBP USE_XCF BUILD_CURVE_TOOLS BUILD_NOISE_TOOLS; do
    if ! grep -q "^${feature}:BOOL=ON$" "$BUILD_DIR/cmake/CMakeCache.txt"; then
      echo "ERROR: required feature $feature was not enabled by CMake" >&2
      grep "^${feature}:" "$BUILD_DIR/cmake/CMakeCache.txt" >&2 || true
      exit 1
    fi
  done
}
assert_features

echo "==> Building"
cmake --build "$BUILD_DIR/cmake" --parallel "$JOBS"

echo "==> Installing into staging prefix"
cmake --install "$BUILD_DIR/cmake"

echo "==> Assembling standalone .app"
mkdir -p "$MACOS_DIR" "$RESOURCES_DIR/etc/gtk-3.0" "$RESOURCES_DIR/share" "$RESOURCES_DIR/lib"

# -L: follow symlinks so every binary lands in MacOS as a real file.
# Otherwise a symlink (e.g. darktable-generate-cache) is skipped by
# `find -type f` and left unsigned, which breaks codesign --deep.
cp -RL "$INSTALL_DIR/bin/." "$MACOS_DIR/"
if [ -d "$INSTALL_DIR/lib" ]; then
  cp -R "$INSTALL_DIR/lib/." "$RESOURCES_DIR/lib/"
fi
if [ -d "$INSTALL_DIR/share" ]; then
  cp -R "$INSTALL_DIR/share/." "$RESOURCES_DIR/share/"
fi
if [ -d "$INSTALL_DIR/libexec/darktable/tools" ]; then
  # Only the compiled noiseprofile helper belongs in MacOS; the shell
  # scripts (subr.sh, darktable-gen-noiseprofile) and .xmp are
  # development-time helpers that cannot be codesigned.
  cp "$INSTALL_DIR/libexec/darktable/tools/darktable-noiseprofile" "$MACOS_DIR/" 2>/dev/null || true
fi

cp "$SCRIPT_DIR/Info.plist" "$CONTENTS_DIR/Info.plist"
cp "$SCRIPT_DIR/Icons.icns" "$RESOURCES_DIR/Icons.icns"
printf 'APPLdarktable\n' > "$CONTENTS_DIR/PkgInfo"
ln -sfn "Resources/share" "$CONTENTS_DIR/share"
ln -sfn "Resources/lib" "$CONTENTS_DIR/lib"

VERSION_FULL="$(grep darktable_package_version "$BUILD_DIR/cmake-build/bin/version_gen.c" 2>/dev/null | cut -d'"' -f2 || true)"
if [ -z "$VERSION_FULL" ]; then
  VERSION_FULL="$(grep darktable_package_version "$BUILD_DIR/cmake/bin/version_gen.c" | cut -d'"' -f2)"
fi
VERSION_BASE="$(printf '%s' "$VERSION_FULL" | cut -d'+' -f1)"
VERSION_COMMIT="$(printf '%s' "$VERSION_FULL" | cut -d'+' -f2 | cut -d'~' -f1)"
[ "$VERSION_BASE" = "$VERSION_COMMIT" ] && VERSION_COMMIT="0"
perl -0pi -e "s/\{VERSION\}/$VERSION_BASE/g; s/\{COMMITS\}/$VERSION_COMMIT/g" "$CONTENTS_DIR/Info.plist"

cat > "$RESOURCES_DIR/etc/gtk-3.0/settings.ini" <<'EOF'
[Settings]
gtk-icon-theme-name = Adwaita
gtk-menu-images = true
gtk-button-images = true
EOF

echo "==> Copying runtime-loadable Homebrew modules and data"
copy_homebrew_lib_tree() {
  local name="$1"
  if [ -e "$HB_PREFIX/lib/$name" ]; then
    mkdir -p "$RESOURCES_DIR/lib"
    rm -rf "$RESOURCES_DIR/lib/$name"
    cp -RL "$HB_PREFIX/lib/$name" "$RESOURCES_DIR/lib/$name"
  fi
}

copy_homebrew_share_tree() {
  local name="$1"
  if [ -e "$HB_PREFIX/share/$name" ]; then
    mkdir -p "$RESOURCES_DIR/share"
    rm -rf "$RESOURCES_DIR/share/$name"
    cp -RL "$HB_PREFIX/share/$name" "$RESOURCES_DIR/share/$name"
  fi
}

copy_homebrew_lib_tree "gtk-3.0"
copy_homebrew_lib_tree "gdk-pixbuf-2.0"
copy_homebrew_lib_tree "gio"
copy_homebrew_lib_tree "libgphoto2"
copy_homebrew_lib_tree "libgphoto2_port"
for gm_dir in "$HB_PREFIX"/lib/GraphicsMagick-*; do
  [ -e "$gm_dir" ] && copy_homebrew_lib_tree "$(basename "$gm_dir")"
done
copy_homebrew_lib_tree "libheif"
[ -e "$HB_PREFIX/opt/sdl3/lib/libSDL3.dylib" ] && cp -L "$HB_PREFIX/opt/sdl3/lib/libSDL3.dylib" "$RESOURCES_DIR/lib/libSDL3.dylib"

for share_name in icons iso-codes mime lensfun themes; do
  copy_homebrew_share_tree "$share_name"
done
mkdir -p "$RESOURCES_DIR/share/locale"
[ -d "$HB_PREFIX/share/locale" ] && cp -RL "$HB_PREFIX/share/locale/." "$RESOURCES_DIR/share/locale/"

mkdir -p "$RESOURCES_DIR/share/glib-2.0/schemas"
cp -L "$HB_PREFIX"/share/glib-2.0/schemas/*.gschema.xml "$RESOURCES_DIR/share/glib-2.0/schemas/" 2>/dev/null || true
"$HB_PREFIX/bin/glib-compile-schemas" "$RESOURCES_DIR/share/glib-2.0/schemas"

# Refresh lens definitions, then include both Homebrew's data and user-updated data.
lensfun-update-data >/dev/null 2>&1 || true
if [ -d "$HOME/.local/share/lensfun/updates/version_1" ]; then
  mkdir -p "$RESOURCES_DIR/share/lensfun"
  cp -R "$HOME/.local/share/lensfun/updates/version_1/." "$RESOURCES_DIR/share/lensfun/" || true
fi

# Generate caches with bundle-relative paths.
if [ -x "$HB_PREFIX/bin/gtk-query-immodules-3.0" ]; then
  "$HB_PREFIX/bin/gtk-query-immodules-3.0" "$RESOURCES_DIR"/lib/gtk-3.0/3.0.0/immodules/*.so \
    2>/dev/null | sed "s#$RESOURCES_DIR#@executable_path/../Resources#g" \
    > "$RESOURCES_DIR/etc/gtk-3.0/gtk.immodules" || true
fi
if [ -x "$HB_PREFIX/bin/gdk-pixbuf-query-loaders" ]; then
  GDK_PIXBUF_MODULEDIR="$RESOURCES_DIR/lib/gdk-pixbuf-2.0/2.10.0/loaders" \
    "$HB_PREFIX/bin/gdk-pixbuf-query-loaders" \
    | sed "s#$RESOURCES_DIR#@executable_path/../Resources#g" \
    > "$RESOURCES_DIR/etc/gtk-3.0/loaders.cache" || true
fi

echo "==> Rewriting Mach-O dependencies to portable @loader_path/@rpath references"
python3 - "$APP_DIR" "$HB_PREFIX" "$BUILD_DIR" <<'PY'
import os
import pathlib
import re
import shutil
import subprocess
import sys

app = pathlib.Path(sys.argv[1]).resolve()
homebrew = pathlib.Path(sys.argv[2]).resolve()
build = pathlib.Path(sys.argv[3]).resolve()
contents = app / "Contents"
macos = contents / "MacOS"
resources = contents / "Resources"
lib_root = resources / "lib"

macho_files = []
for path in list(macos.glob("*")):
    if path.is_file() and not path.is_symlink():
        macho_files.append(path)
for path in lib_root.rglob("*"):
    if not path.is_file() or path.is_symlink():
        continue
    if path.suffix in (".dylib", ".so") or os.access(path, os.X_OK):
        macho_files.append(path)

def run_otool(arguments):
    result = subprocess.run(["otool", *arguments], capture_output=True, text=True)
    return result.stdout if result.returncode == 0 else ""

def is_macho(path):
    return run_otool(["-L", str(path)]) != ""

macho_files = [p for p in macho_files if is_macho(p)]
macho_set = {p.resolve() for p in macho_files}

index = {}
for path in lib_root.rglob("*"):
    if path.is_file() and not path.is_symlink() and (path.suffix in (".dylib", ".so") or os.access(path, os.X_OK)):
        index.setdefault(path.name, []).append(path)

homebrew_index = {}
for pattern in ("lib/*.dylib", "opt/*/lib/*.dylib", "lib/*/*.dylib"):
    for path in homebrew.glob(pattern):
        if path.is_file() and not path.is_symlink():
            homebrew_index.setdefault(path.name, []).append(path)
        elif path.is_symlink():
            # Keep unversioned symlink names too (e.g. libicuuc.78.dylib):
            # Homebrew dylibs reference each other via those names.
            homebrew_index.setdefault(path.name, []).append(path)

def rpaths_for(path):
    data = run_otool(["-l", str(path)])
    rpaths = []
    for line in data.splitlines():
        stripped = line.strip()
        if stripped.startswith("path "):
            value = stripped.split(" (offset ", 1)[0][5:].strip()
            rpaths.append(value)
    return rpaths

def expand_token(value, path):
    file_dir = path.parent
    # Homebrew modules frequently carry rpaths such as
    # @loader_path/../../../../opt/gettext/lib. Resolve these to the actual
    # Homebrew prefix so transitive dependencies can be bundled.
    opt_match = re.match(r"@(loader|executable)_path/((?:\.\./)+)opt/(.+)$", value)
    if opt_match:
        return homebrew / "opt" / opt_match.group(3)
    if value.startswith("@executable_path/"):
        return macos / value[len("@executable_path/"):]
    if value.startswith("@loader_path/"):
        return file_dir / value[len("@loader_path/"):]
    if value == "@executable_path":
        return macos
    if value == "@loader_path":
        return file_dir
    return pathlib.Path(value)

def resolve(dep, path, rpaths):
    if dep.startswith("@rpath/"):
        suffix = dep[len("@rpath/"):]
        name = pathlib.Path(suffix).name
        candidates = []
        for rpath in rpaths:
            candidates.append(expand_token(rpath, path) / suffix)
        candidates.extend([
            path.parent / suffix,
            lib_root / suffix,
            lib_root / "darktable" / suffix,
            resources / suffix,
            homebrew / "lib" / suffix,
        ])
        for candidate in candidates:
            if candidate.exists():
                return candidate.resolve()
        matches = index.get(name) or homebrew_index.get(name, [])
        if matches:
            return matches[0].resolve()
        return None
    candidate = expand_token(dep, path)
    if candidate.exists():
        return candidate.resolve()
    # Fallback: Homebrew dylibs reference each other via unversioned symlink
    # names (e.g. @loader_path/libicuuc.78.dylib). Resolve the name in the
    # Homebrew tree so the versioned real file gets bundled and the reference
    # rewritten to the versioned name.
    name = pathlib.Path(dep).name
    matches = index.get(name) or homebrew_index.get(name, [])
    if matches:
        return matches[0].resolve()
    return None

def is_system(path):
    system_prefixes = ("/usr/lib/", "/System/Library/", "/System/iOSSupport/", "/usr/share/")
    text = str(path)
    return any(text.startswith(prefix) for prefix in system_prefixes)

def copy_external(source):
    target = lib_root / source.name
    if not target.exists():
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        if target not in macho_files and is_macho(target):
            macho_files.append(target)
            macho_set.add(target.resolve())
            index.setdefault(target.name, []).append(target)
    return target

def relative_loader_reference(path, target):
    return "@loader_path/" + os.path.relpath(target, path.parent)

changes = []
queue = list(macho_files)
processed = set()
while queue:
    path = queue.pop(0)
    if path in processed:
        continue
    processed.add(path)
    rpaths = rpaths_for(path)
    deps_output = run_otool(["-L", str(path)])
    lines = deps_output.splitlines()[1:]
    for line in lines:
        raw = line.split(" (compatibility", 1)[0].strip()
        if not raw:
            continue
        source = resolve(raw, path, rpaths)
        if source is None or is_system(source):
            continue
        try:
            source.relative_to(app)
            target = source
        except ValueError:
            target = copy_external(source)
        if target not in processed:
            queue.append(target)
        new_reference = relative_loader_reference(path, target)
        if raw != new_reference:
            changes.append((path, raw, new_reference))

for path, old, new in changes:
    subprocess.run(["install_name_tool", "-change", old, new, str(path)], check=False)

# Remove build/Homebrew rpaths and add portable rpaths used by dlopen() and @rpath.
for path in macho_files:
    for rpath in rpaths_for(path):
        if str(homebrew) in rpath or str(build) in rpath or rpath.startswith("/"):
            subprocess.run(["install_name_tool", "-delete_rpath", rpath, str(path)], check=False)
    rel_lib = os.path.relpath(lib_root, path.parent)
    rel_darktable_lib = os.path.relpath(lib_root / "darktable", path.parent)
    for rpath in (f"@loader_path/{rel_lib}", f"@loader_path/{rel_darktable_lib}"):
        subprocess.run(["install_name_tool", "-add_rpath", rpath, str(path)], check=False)

# Give bundled dylibs stable @rpath identifiers.
for path in macho_files:
    if path.suffix in (".dylib", ".so"):
        try:
            relative = path.relative_to(lib_root)
            identifier = "@rpath/" + str(relative)
            subprocess.run(["install_name_tool", "-id", identifier, str(path)], check=False)
        except ValueError:
            pass

print(f"Processed {len(macho_files)} Mach-O files and {len(changes)} dependency references")
PY

echo "==> Ad-hoc signing the bundle"
# Resolve any symlinks under MacOS first: codesign cannot sign a symlink
# and `find -type f` skips it, which made --deep fail with
# "code object is not signed at all / In subcomponent: darktable-generate-cache".
find "$MACOS_DIR" -type l -print0 2>/dev/null | while IFS= read -r -d '' link; do
  target="$(readlink "$link")"
  case "$target" in
    /*) real="$target" ;;
    *)  real="$(dirname "$link")/$target" ;;
  esac
  if [ -f "$real" ]; then
    rm -f "$link"
    cp "$real" "$link"
  fi
done
# Sign deepest files first, then the app envelope.
find "$RESOURCES_DIR/lib" -type f \( -name '*.dylib' -o -name '*.so' -o -perm +111 \) -print0 2>/dev/null \
  | xargs -0 -I{} codesign --force --timestamp=none --sign - "{}" 2>/dev/null || true
find "$MACOS_DIR" -type f -print0 | xargs -0 -I{} codesign --force --timestamp=none --sign - "{}"
# Envelope sign without --deep (deprecated in Xcode 16): every payload has
# already been signed individually above, so the outer signature is enough.
codesign --force --timestamp=none --sign - "$APP_DIR"

echo "==> Verifying the app"
codesign --verify --deep --strict --verbose=2 "$APP_DIR"
"$MACOS_DIR/darktable-cli" --version
for required in \
  "$RESOURCES_DIR/Icons.icns" \
  "$RESOURCES_DIR/share/darktable/kernels" \
  "$RESOURCES_DIR/share/darktable/negadoctor_ai" \
  "$RESOURCES_DIR/share/darktable/lua-scripts" \
  "$RESOURCES_DIR/lib"; do
  test -e "$required"
done
if find "$APP_DIR" -type f -perm +111 -print0 2>/dev/null | xargs -0 otool -L 2>/dev/null | grep -v ":$" | grep -v "is not an object file" | grep -E "($BUILD_DIR|$HB_PREFIX/Cellar)" ; then
  echo "ERROR: portable bundle still contains build-tree or Homebrew/Cellar references" >&2
  exit 1
fi

echo "==> Creating DMG"
DMG_ROOT="$STAGE_DIR/dmg-root"
rm -rf "$DMG_ROOT"
mkdir -p "$DMG_ROOT/.background"
cp -R "$APP_DIR" "$DMG_ROOT/"
ln -s /Applications "$DMG_ROOT/Applications"
cp "$SCRIPT_DIR/macos_install_background.png" "$DMG_ROOT/.background/"

TEMP_DMG="$STAGE_DIR/pack.temp.dmg"
rm -f "$TEMP_DMG"
success=0
for attempt in 1 2 3 4 5 6 7 8; do
  if hdiutil create -srcfolder "$DMG_ROOT" -volname "darktable" -fs HFS+ \
      -fsargs "-c c=64,a=16,e=16" -format UDRW "$TEMP_DMG"; then
    success=1
    break
  fi
  sleep 1
done
if [ "$success" -ne 1 ]; then
  sudo pkill -9 XProtect >/dev/null 2>&1 || true
  sleep 3
  hdiutil create -srcfolder "$DMG_ROOT" -volname "darktable" -fs HFS+ \
    -fsargs "-c c=64,a=16,e=16" -format UDRW "$TEMP_DMG"
fi

device="$(hdiutil attach -readwrite -noverify -noautoopen "$TEMP_DMG" | egrep '^/dev/' | sed 1q | awk '{print $1}')"
trap 'hdiutil detach "$device" >/dev/null 2>&1 || true' EXIT
osascript <<'APPLESCRIPT'
tell application "Finder"
  tell disk "darktable"
    open
    set current view of container window to icon view
    set toolbar visible of container window to false
    set statusbar visible of container window to false
    set the bounds of container window to {400, 100, 900, 430}
    set theViewOptions to the icon view options of container window
    set arrangement of theViewOptions to not arranged
    set icon size of theViewOptions to 72
    set background picture of theViewOptions to file ".background:macos_install_background.png"
    set position of item "darktable.app" of container window to {100, 150}
    set position of item "Applications" of container window to {400, 150}
    update without registering applications
  end tell
end tell
APPLESCRIPT
chmod -Rf go-w "/Volumes/darktable"
sync
hdiutil detach "$device"
trap - EXIT

rm -f "$OUT_DIR/$DMG_NAME"
hdiutil convert "$TEMP_DMG" -format UDZO -imagekey zlib-level=9 -o "$OUT_DIR/$DMG_NAME"
codesign --force --timestamp=none --sign - "$OUT_DIR/$DMG_NAME" || true

echo "==> Built $OUT_DIR/$DMG_NAME"
