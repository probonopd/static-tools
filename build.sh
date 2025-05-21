#!/bin/sh

set -ex

if ! command -v apk; then
	echo "This script should be run in an Alpine container"
	exit 1
fi

# Minimize binary size
export CFLAGS="-ffunction-sections -fdata-sections -Os"

apk update
apk add alpine-sdk util-linux strace file autoconf automake libtool xz

# Build static libfuse3 with patch for https://github.com/AppImage/type2-runtime/issues/10
apk add eudev-dev gettext-dev linux-headers meson # From https://git.alpinelinux.org/aports/tree/main/fuse3/APKBUILD
wget -c -q "https://github.com/libfuse/libfuse/releases/download/fuse-3.15.0/fuse-3.15.0.tar.xz"
tar xf fuse-3.*.tar.xz
cd fuse-3.*/
patch -p1 < ../patches/libfuse/mount.c.diff
mkdir build
cd build
meson setup --prefix=/usr ..
meson configure --default-library static
ninja install
cd ../../

# Build static squashfuse
apk add zstd-dev zstd-static zlib-dev zlib-static # fuse-dev fuse-static fuse3-static fuse3-dev
wget -c -q "https://github.com/vasi/squashfuse/archive/e51978c.tar.gz"
tar xf e51978c.tar.gz
cd squashfuse-*/
./autogen.sh
./configure --help
./configure CFLAGS=-no-pie LDFLAGS=-static
make -j$(nproc)
make install
/usr/bin/install -c -m 644 *.h '/usr/local/include/squashfuse' # ll.h
cd -

# Build static AppImage runtime
export GIT_COMMIT=$(cat src/runtime/version)
cd src/runtime
make runtime-fuse3 -j$(nproc)
file runtime-fuse3
strip runtime-fuse3
ls -lh runtime-fuse3
echo -ne 'AI\x02' | dd of=runtime-fuse3 bs=1 count=3 seek=8 conv=notrunc # magic bytes, always do AFTER strip
cd -

# Build static patchelf
# wget https://github.com/NixOS/patchelf/archive/0.9.tar.gz
# 0.10 cripples my files, puts XXXXX inside; tring 0.18.0
wget https://github.com/NixOS/patchelf/archive/refs/tags/0.18.0.tar.gz
tar xf 0.18.0.tar.gz
cd patchelf-*/
./bootstrap.sh
./configure --prefix=/usr CFLAGS=-no-pie LDFLAGS=-static
make -j$(nproc)
mv src/patchelf .
file patchelf
strip patchelf
ls -lh patchelf
cd -

# Build static zsyncmake
apk add glib-dev glib-static
wget http://zsync.moria.org.uk/download/zsync-0.6.2.tar.bz2
tar xf zsync-*.tar.bz2
cd zsync-*/
find . -type f -exec sed -i -e 's|off_t|size_t|g' {} \;
./configure CFLAGS=-no-pie LDFLAGS=-static --build=$(arch)-unknown-linux-gnu
make -j$(nproc)
file zsyncmake
strip zsyncmake
cd -

# Build static squashfs-tools
apk add zlib-dev zlib-static
wget -O squashfs-tools.tar.gz https://github.com/plougher/squashfs-tools/archive/refs/tags/4.5.1.tar.gz
tar xf squashfs-tools.tar.gz
cd squashfs-tools-*/squashfs-tools
sed -i -e 's|#ZSTD_SUPPORT = 1|ZSTD_SUPPORT = 1|g' Makefile
make -j$(nproc) LDFLAGS=-static
file mksquashfs unsquashfs
strip mksquashfs unsquashfs
cd -

# Build static desktop-file-utils
# apk add glib-static glib-dev
wget -c https://gitlab.freedesktop.org/xdg/desktop-file-utils/-/archive/56d220dd679c7c3a8f995a41a27a7d6f3df49dea/desktop-file-utils-56d220dd679c7c3a8f995a41a27a7d6f3df49dea.tar.gz
tar xf desktop-file-utils-*.tar.gz
cd desktop-file-utils-*/
# The next 2 lines are a workaround for: checking build system type... ./config.guess: unable to guess system type
# These files wer downloaded from https://git.savannah.gnu.org/gitweb/?p=config.git.
# https://git.savannah.gnu.org often gets overloaded and returns a 502 error, so we use a local copy.
cp /patches/desktop-file-utils/config.* ./
autoreconf --install # https://github.com/shendurelab/LACHESIS/issues/31#issuecomment-283963819
./configure CFLAGS=-no-pie LDFLAGS=-static
make -j$(nproc)
cd src/
gcc -static -o desktop-file-validate keyfileutils.o validate.o validator.o mimeutils.o -lglib-2.0 -lintl
gcc -static -o update-desktop-database  update-desktop-database.o mimeutils.o -lglib-2.0 -lintl
gcc -static -o desktop-file-install keyfileutils.o validate.o install.o mimeutils.o -lglib-2.0 -lintl
strip desktop-file-install desktop-file-validate update-desktop-database
cd ../..

# Build appstreamcli
apk add glib-static meson libxml2-dev yaml-dev yaml-static gperf
# Compile liblmdb from source as Alpine only ship it as a .so
wget https://git.openldap.org/openldap/openldap/-/archive/LMDB_0.9.29/openldap-LMDB_0.9.29.tar.gz
tar xf openldap-LMDB_*.tar.gz
cd openldap-LMDB_*/libraries/liblmdb
make liblmdb.a
install -D -m 644 liblmdb.a /usr/local/lib/liblmdb.a
install -D -m 644 lmdb.h /usr/local/include/lmdb.h
cd -
wget -O appstream.tar.gz https://github.com/ximion/appstream/archive/v0.12.9.tar.gz
tar xf appstream.tar.gz
cd appstream-*/
# Ask for static dependencies
sed -i -E -e "s|(dependency\('.*')|\1, static: true|g" meson.build
# Disable po, docs and tests
sed -i -e "s|subdir('po/')||" meson.build
sed -i -e "s|subdir('docs/')||" meson.build
sed -i -e "s|subdir('tests/')||" meson.build
# -no-pie is required to statically link to libc
CFLAGS=-no-pie LDFLAGS=-static meson setup build --buildtype=release --default-library=static --prefix="$(pwd)/prefix" --strip -Db_lto=true -Db_ndebug=if-release -Dstemming=false -Dgir=false -Dapidocs=false
# Install in a staging enviroment
meson install -C build
file prefix/bin/appstreamcli
cd -

# Build static bsdtar
apk add zlib-dev zlib-static bzip2-dev bzip2-static xz-dev
wget https://www.libarchive.org/downloads/libarchive-3.3.2.tar.gz
tar xf libarchive-*.tar.gz
cd libarchive-*/
./configure --disable-shared --enable-bsdtar=static --disable-bsdcat --disable-bsdcpio --with-zlib --without-bz2lib --disable-maintainer-mode --disable-dependency-tracking CFLAGS=-no-pie LDFLAGS=-static
make -j$(nproc)
gcc -static -o bsdtar tar/bsdtar-bsdtar.o tar/bsdtar-cmdline.o tar/bsdtar-creation_set.o tar/bsdtar-read.o tar/bsdtar-subst.o tar/bsdtar-util.o tar/bsdtar-write.o .libs/libarchive.a .libs/libarchive_fe.a /lib/libz.a -llzma
strip bsdtar
cd -

# Build static dwarfs
# We first need newer packages
echo "http://dl-cdn.alpinelinux.org/alpine/v3.20/main
http://dl-cdn.alpinelinux.org/alpine/v3.20/community" >> /etc/apk/repositories
apk update && apk upgrade
apk add cmake \
	xz-static \
	lz4-dev lz4-static \
	acl-dev acl-static \
  libevent-dev libevent-static \
  zstd-dev zstd-static \
  nlohmann-json \
  date-dev \
  utfcpp \
	openssl-dev openssl-libs-static \
	libarchive-dev libarchive-static \
	bzip2-static \
	expat-static \
	upx \
	boost1.77-dev boost1.77-chrono boost1.77-context boost1.77-filesystem boost-iostreams boost1.77-program_options boost1.77-regex boost1.77-system boost1.77-thread boost1.77-static
# We also need to build a static version of double-conversion
git clone --depth=1 --branch v3.3.1 https://github.com/google/double-conversion
cd double-conversion
cmake . && make && make install
cd /
# And glog
git clone --depth=1 --branch v0.7.1 https://github.com/google/glog
cd glog
cmake -S . -DBUILD_SHARED_LIBS=OFF
make && make install
cd /
# And xxhash
git clone --depth=1 --branch=v0.8.3 https://github.com/Cyan4973/xxHash
cd xxHash
make && make install
cd /
# And libunwind. The libunwind from alpine doesn't work correctly all of the time on all platforms.
git clone --depth=1 --branch=v1.8.1 https://github.com/libunwind/libunwind
cd libunwind
autoreconf -i
./configure --prefix=/usr/local
make
make install
# Actually build dwarfs
wget https://github.com/mhx/dwarfs/releases/download/v0.12.4/dwarfs-0.12.4.tar.xz
tar xf dwarfs-*.tar.xz
cd dwarfs-*/
# Patch out avx2 requirement
patch -i /patches/dwarfs/libdwarfs_tool.diff ./cmake/libdwarfs_tool.cmake
mkdir build
cd build
cmake .. -GNinja -DSTATIC_BUILD_DO_NOT_USE=ON -DWITH_UNIVERSAL_BINARY=ON
ninja
file universal/dwarfs-universal
strip universal/dwarfs-universal
cd /


mkdir -p out
cp src/runtime/runtime-fuse3 out/runtime-fuse3-$ARCHITECTURE
cp patchelf-*/patchelf out/patchelf-$ARCHITECTURE
cp zsync-*/zsyncmake out/zsyncmake-$ARCHITECTURE
cp squashfs-tools-*/squashfs-tools/mksquashfs out/mksquashfs-$ARCHITECTURE
cp squashfs-tools-*/squashfs-tools/unsquashfs out/unsquashfs-$ARCHITECTURE
cp libarchive-*/bsdtar out/bsdtar-$ARCHITECTURE
cp desktop-file-utils-*/src/desktop-file-install out/desktop-file-install-$ARCHITECTURE
cp desktop-file-utils-*/src/desktop-file-validate out/desktop-file-validate-$ARCHITECTURE
cp desktop-file-utils-*/src/update-desktop-database out/update-desktop-database-$ARCHITECTURE
cp appstream-*/prefix/bin/appstreamcli out/appstreamcli-$ARCHITECTURE
cp dwarfs-*/build/universal/dwarfs-universal out/dwarfs-$ARCHITECTURE
