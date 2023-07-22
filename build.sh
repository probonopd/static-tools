#!/bin/sh

set -ex

if ! command -v apk; then
	echo "This script should be run in an Alpine container"
	exit 1
fi

apk update
apk add alpine-sdk util-linux strace file autoconf automake libtool

# Build static squashfuse
apk add fuse-dev fuse-static zstd-dev zstd-static zlib-dev zlib-static # fuse3-static fuse3-dev
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
make runtime-fuse2 -j$(nproc)
file runtime-fuse2
strip runtime-fuse2
ls -lh runtime-fuse2
echo -ne 'AI\x02' | dd of=runtime-fuse2 bs=1 count=3 seek=8 conv=notrunc # magic bytes, always do AFTER strip
cd -

# Build static patchelf
wget https://github.com/NixOS/patchelf/archive/0.9.tar.gz # 0.10 cripples my files, puts XXXXX inside
tar xf 0.9.tar.gz 
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
wget 'http://git.savannah.gnu.org/gitweb/?p=config.git;a=blob_plain;f=config.guess;hb=HEAD' -O config.guess
wget 'http://git.savannah.gnu.org/gitweb/?p=config.git;a=blob_plain;f=config.sub;hb=HEAD' -O config.sub
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

mkdir -p out
cp src/runtime/runtime-fuse2 out/runtime-fuse2-$ARCHITECTURE
cp patchelf-*/patchelf out/patchelf-$ARCHITECTURE
cp zsync-*/zsyncmake out/zsyncmake-$ARCHITECTURE
cp squashfs-tools-*/squashfs-tools/mksquashfs out/mksquashfs-$ARCHITECTURE
cp squashfs-tools-*/squashfs-tools/unsquashfs out/unsquashfs-$ARCHITECTURE
cp libarchive-*/bsdtar out/bsdtar-$ARCHITECTURE
cp desktop-file-utils-*/src/desktop-file-install out/desktop-file-install-$ARCHITECTURE
cp desktop-file-utils-*/src/desktop-file-validate out/desktop-file-validate-$ARCHITECTURE
cp desktop-file-utils-*/src/update-desktop-database out/update-desktop-database-$ARCHITECTURE
cp appstream-*/prefix/bin/appstreamcli out/appstreamcli-$ARCHITECTURE

# Build static strace
mkdir -p out
apk add xz gawk glib-dev
wget https://github.com/strace/strace/releases/download/v6.4/strace-6.4.tar.xz
tar xf strace-*.tar.xz
cd strace-*
LDFLAGS='-static -pthread' ./configure --disable-mpers
make -j$(nproc)
cp src/strace ../out/strace-$ARCHITECTURE
cd -

# Build static iproute2 tools
mkdir -p out
apk add glib-dev bison flex
wget https://mirrors.edge.kernel.org/pub/linux/utils/net/iproute2/iproute2-6.4.0.tar.gz
tar xf iproute2-*.tar.gz
cd iproute2-*
LDFLAGS='-static' ./configure
LDFLAGS='-static' make -j$(nproc)

cp ip/ip ../out/ip-$ARCHITECTURE
for tool in ifstat lnstat nstat rtacct ss
do
    cp misc/$tool ../out/$tool-$ARCHITECTURE
done
cd -

# Build procps tools
out=$PWD/out
mkdir -p $out
apk add gettext gettext-dev ncurses ncurses-dev ncurses-static glib-dev glib-static
wget https://gitlab.com/procps-ng/procps/-/archive/v4.0.3/procps-v4.0.3.tar.gz
tar xf procps-*
cd procps-*
./autogen.sh
LDFLAGS='-lintl --static' ./configure --enable-static --disable-shared --disable-w --prefix /tmp/procps
LDFLAGS='-lintl --static' make -j$(nproc) install

cd /tmp/procps/bin/
for tool in *
do
    cp $tool $out/$tool-$ARCHITECTURE
done
cd -
cd -
