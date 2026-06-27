#!/bin/bash
set -e

# Build directory name
BUILD_DIR="build_deb"
# Packaging directory name
PKG_DIR="deb_pkg"
# Installation prefix inside package
PREFIX="/opt/aegisub"

echo "=== Cleaning previous builds ==="
rm -rf "$BUILD_DIR" "$PKG_DIR" *.deb

echo "=== Configuring Meson ==="
# We use prefix /opt/aegisub to bundle everything there
meson setup "$BUILD_DIR" \
  --prefix="$PREFIX" \
  --buildtype=release \
  -Ddefault_library=static \
  -Dbuild_appimage=false \
  --force-fallback-for=ffms2 \
  -Dffmpeg:libdav1d=enabled \
  -Davisynth=disabled \
  -Dbestsource=enabled \
  -Dvapoursynth=enabled

echo "=== Compiling Aegisub ==="
meson compile -C "$BUILD_DIR"

echo "=== Installing to temporary package root ==="
mkdir -p "$PKG_DIR"
# meson install will place files under $PKG_DIR/opt/aegisub/...
meson install -C "$BUILD_DIR" --destdir="$(pwd)/$PKG_DIR"

echo "=== Integrating with system paths ==="
# 1. Create a system symlink in /usr/bin/aegisub pointing to /opt/aegisub/bin/aegisub
mkdir -p "$PKG_DIR/usr/bin"
ln -sf "$PREFIX/bin/aegisub" "$PKG_DIR/usr/bin/aegisub"

# 2. Copy the desktop entry to /usr/share/applications for system-wide recognition
mkdir -p "$PKG_DIR/usr/share/applications"
cp "$PKG_DIR/opt/aegisub/share/applications/aegisub.desktop" "$PKG_DIR/usr/share/applications/"

# Modify Exec and Icon paths in the system desktop entry to use /opt/aegisub
sed -i "s|Exec=aegisub|Exec=$PREFIX/bin/aegisub|g" "$PKG_DIR/usr/share/applications/aegisub.desktop"
sed -i "s|Icon=aegisub|Icon=$PREFIX/share/icons/hicolor/64x64/apps/aegisub.png|g" "$PKG_DIR/usr/share/applications/aegisub.desktop"

echo "=== Creating Debian control files ==="
mkdir -p "$PKG_DIR/DEBIAN"

# Create the package control file
cat <<EOF > "$PKG_DIR/DEBIAN/control"
Package: aegisub-portable
Version: 3.2.2
Section: video
Priority: optional
Architecture: amd64
Maintainer: Custom Build <custom@build.local>
Depends: libx11-6, libfreetype6, libfontconfig1, libass9 | libass5, libasound2, libffms2-5 | libffms2-4, libhunspell-1.7-0 | libhunspell-1.6-0, libuchardet0, libpulse0, libopenal1, libxxhash0, libwxgtk3.2-1t64 | libwxgtk3.0-gtk3-0v5, libwxgtk-gl3.2-1t64 | libwxgtk-gl3.0-gtk3-0v5
Description: Aegisub subtitle editor packaged portably in /opt/aegisub.
 This allows easy installation via .deb while keeping all internal folders
 (automation scripts, locales, dictionaries) inside /opt/aegisub for easy management.
EOF

# Create postinst and prerm script to update desktop database and icon caches
cat <<'EOF' > "$PKG_DIR/DEBIAN/postinst"
#!/bin/sh
set -e
if [ "$1" = "configure" ]; then
    echo "Updating desktop database..."
    update-desktop-database -q || true
fi
EOF

cat <<'EOF' > "$PKG_DIR/DEBIAN/prerm"
#!/bin/sh
set -e
if [ "$1" = "remove" ]; then
    echo "Cleaning up..."
fi
EOF

chmod 755 "$PKG_DIR/DEBIAN/postinst" "$PKG_DIR/DEBIAN/prerm"

echo "=== Packaging as .deb ==="
# dpkg-deb expects correct permissions
chmod -R g-w "$PKG_DIR"

# Build the .deb package
dpkg-deb --build "$PKG_DIR" aegisub-portable_3.2.2_amd64.deb

echo "=== Cleaning up build files ==="
rm -rf "$PKG_DIR"

echo "============================================="
echo "Success! Package created: aegisub-portable_3.2.2_amd64.deb"
echo "You can install it using: sudo dpkg -i aegisub-portable_3.2.2_amd64.deb"
echo "All application folders are located in: /opt/aegisub"
echo "============================================="
