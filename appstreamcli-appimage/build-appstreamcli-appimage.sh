#!/bin/bash

set -ex

# This script builds appstreamcli AppImages for AppStream 1.0
# It uses go-appimage's appimagetool with -s deploy to bundle everything including glibc

APPSTREAM_VERSION="${APPSTREAM_VERSION:-1.0.6}"
ARCH="${ARCH:-x86_64}"

# Install build dependencies
apt-get update
apt-get install -y wget curl build-essential git meson ninja-build pkg-config \
    libglib2.0-dev libxml2-dev libyaml-dev gperf libcurl4-openssl-dev \
    libsystemd-dev desktop-file-utils file

# Build liblmdb from source (needed for AppStream)
wget https://git.openldap.org/openldap/openldap/-/archive/LMDB_0.9.29/openldap-LMDB_0.9.29.tar.gz
tar xf openldap-LMDB_*.tar.gz
cd openldap-LMDB_*/libraries/liblmdb
make
make prefix=/usr install
cd ../../..

# Download and build AppStream 1.0.x
wget -O appstream.tar.gz "https://github.com/ximion/appstream/archive/v${APPSTREAM_VERSION}.tar.gz"
tar xf appstream.tar.gz
cd appstream-${APPSTREAM_VERSION}/

# Build AppStream
meson setup build --buildtype=release --prefix=/usr
meson install -C build

cd ..

# Create AppDir structure
mkdir -p AppDir/usr/bin
mkdir -p AppDir/usr/share/metainfo

# Copy appstreamcli binary to AppDir
cp /usr/bin/appstreamcli AppDir/usr/bin/

# Create desktop file
cat > AppDir/appstreamcli.desktop << 'EOF'
[Desktop Entry]
Type=Application
Name=AppStream CLI
Comment=Command-line interface for AppStream
Exec=appstreamcli
Icon=appstreamcli
Categories=Development;
Terminal=true
EOF

# Create a simple icon (using convert from ImageMagick if available, otherwise create SVG)
if command -v convert > /dev/null 2>&1; then
    convert -size 256x256 xc:blue -pointsize 72 -fill white -gravity center -annotate +0+0 'AS' AppDir/appstreamcli.png
else
    # Create SVG icon
    cat > AppDir/appstreamcli.svg << 'SVGEOF'
<?xml version="1.0" encoding="UTF-8"?>
<svg width="256" height="256" xmlns="http://www.w3.org/2000/svg">
  <rect width="256" height="256" fill="#4A90E2"/>
  <text x="128" y="150" font-family="Arial" font-size="100" fill="white" text-anchor="middle">AS</text>
</svg>
SVGEOF
fi

# Create AppRun script
cat > AppDir/AppRun << 'APPRUNEOF'
#!/bin/bash
SELF=$(readlink -f "$0")
HERE=${SELF%/*}
export PATH="${HERE}/usr/bin:${HERE}/usr/sbin:${HERE}/usr/games:${HERE}/bin:${HERE}/sbin:${PATH:+:$PATH}"
export LD_LIBRARY_PATH="${HERE}/usr/lib:${HERE}/usr/lib/i386-linux-gnu:${HERE}/usr/lib/x86_64-linux-gnu:${HERE}/usr/lib32:${HERE}/usr/lib64:${HERE}/lib:${HERE}/lib/i386-linux-gnu:${HERE}/lib/x86_64-linux-gnu:${HERE}/lib32:${HERE}/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PYTHONPATH="${HERE}/usr/share/pyshared/${PYTHONPATH:+:$PYTHONPATH}"
export XDG_DATA_DIRS="${HERE}/usr/share:${XDG_DATA_DIRS:+:$XDG_DATA_DIRS}"
export PERLLIB="${HERE}/usr/share/perl5:${HERE}/usr/lib/perl5:${PERLLIB:+:$PERLLIB}"
export GSETTINGS_SCHEMA_DIR="${HERE}/usr/share/glib-2.0/schemas:${GSETTINGS_SCHEMA_DIR:+:$GSETTINGS_SCHEMA_DIR}"
export QT_PLUGIN_PATH="${HERE}/usr/lib/qt4/plugins:${HERE}/usr/lib/i386-linux-gnu/qt4/plugins:${HERE}/usr/lib/x86_64-linux-gnu/qt4/plugins:${HERE}/usr/lib32/qt4/plugins:${HERE}/usr/lib64/qt4/plugins:${HERE}/usr/lib/qt5/plugins:${HERE}/usr/lib/i386-linux-gnu/qt5/plugins:${HERE}/usr/lib/x86_64-linux-gnu/qt5/plugins:${HERE}/usr/lib32/qt5/plugins:${HERE}/usr/lib64/qt5/plugins:${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
EXEC=$(grep -e '^Exec=.*' "${HERE}"/*.desktop | head -n 1 | cut -d "=" -f 2- | sed -e 's|%.||g')
exec ${EXEC} "$@"
APPRUNEOF
chmod +x AppDir/AppRun

# Download appimagetool for the current architecture
APPIMAGETOOL_ARCH="${ARCH}"
if [ "${ARCH}" = "x86" ]; then
    APPIMAGETOOL_ARCH="i686"
fi
wget -c "https://github.com/probonopd/go-appimage/releases/download/continuous/appimagetool-${APPIMAGETOOL_ARCH}.AppImage"
chmod +x appimagetool-${APPIMAGETOOL_ARCH}.AppImage

# Extract appimagetool (it's an AppImage itself)
./appimagetool-${APPIMAGETOOL_ARCH}.AppImage --appimage-extract
mv squashfs-root appimagetool-extracted

# Use appimagetool with -s deploy to create AppImage with bundled libraries
ARCH=${ARCH} ./appimagetool-extracted/AppRun -s deploy AppDir appstreamcli-${APPSTREAM_VERSION}-${ARCH}.AppImage

# Test the AppImage
./appstreamcli-${APPSTREAM_VERSION}-${ARCH}.AppImage --version || true

echo "AppImage created: appstreamcli-${APPSTREAM_VERSION}-${ARCH}.AppImage"
ls -lh appstreamcli-${APPSTREAM_VERSION}-${ARCH}.AppImage
file appstreamcli-${APPSTREAM_VERSION}-${ARCH}.AppImage
