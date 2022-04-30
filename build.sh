#!/bin/sh

set -ex

#############################################
# Download and extract minimal Alpine system
#############################################

wget http://dl-cdn.alpinelinux.org/alpine/v3.10/releases/$ARCHITECTURE/alpine-minirootfs-3.10.2-$ARCHITECTURE.tar.gz
sudo rm -rf ./miniroot  true # Clean up from previous runs
mkdir -p ./miniroot
cd ./miniroot
sudo tar xf ../alpine-minirootfs-3.10.2-$ARCHITECTURE.tar.gz
cd -

#############################################
# Build static patchelf
# https://github.com/NixOS/patchelf/issues/185
#############################################

wget https://github.com/NixOS/patchelf/archive/0.9.tar.gz # 0.10 cripples my files, puts XXXXX inside
tar xf 0.9.tar.gz
cd patchelf-*/
sudo apt install autoconf
./bootstrap.sh
./configure --prefix=/usr
make -j$(nproc) LDFLAGS=-static
file src/patchelf
sudo cp src/patchelf ../miniroot/usr/bin/ # Make available inside the chroot too
cd -

#############################################
# Prepare chroot
#############################################

sudo mount -o bind /dev miniroot/dev
sudo mount -t proc none miniroot/proc
sudo mount -t sysfs none miniroot/sys
sudo cp -p /etc/resolv.conf miniroot/etc/
sudo chroot miniroot /bin/sh -ex <<\EOF

#############################################
# Now inside chroot
#############################################

# Install build dependencies
apk update
apk add alpine-sdk bash util-linux strace file zlib-dev autoconf automake libtool

# Build static squashfs-tools
wget -c http://deb.debian.org/debian/pool/main/s/squashfs-tools/squashfs-tools_4.4.orig.tar.gz
tar xf squashfs-tools_4.4.orig.tar.gz
cd squashfs-tools-4.4/squashfs-tools
make -j$(nproc)
gcc -static mksquashfs.o read_fs.o action.o swap.o pseudo.o compressor.o sort.o progressbar.o read_file.o info.o restore.o process_fragments.o caches-queues-lists.o gzip_wrapper.o xattr.o read_xattrs.o -lm -lz -o mksquashfs
gcc -static unsquashfs.o unsquash-1.o unsquash-2.o unsquash-3.o unsquash-4.o unsquash-123.o unsquash-34.o swap.o compressor.o unsquashfs_info.o gzip_wrapper.o read_xattrs.o unsquashfs_xattr.o -lm -lz -o unsquashfs
strip mksquashfs unsquashfs
cd ../../

# Build static desktop-file-utils
apk add glib-static glib-dev
wget -c https://www.freedesktop.org/software/desktop-file-utils/releases/desktop-file-utils-0.15.tar.gz
tar xf desktop-file-utils-0.15.tar.gz
cd desktop-file-utils-0.15
# The next 2 lines are a workaround for: checking build system type... ./config.guess: unable to guess system type
wget 'http://git.savannah.gnu.org/gitweb/?p=config.git;a=blob_plain;f=config.guess;hb=HEAD' -O config.guess
wget 'http://git.savannah.gnu.org/gitweb/?p=config.git;a=blob_plain;f=config.sub;hb=HEAD' -O config.sub
autoreconf --install # https://github.com/shendurelab/LACHESIS/issues/31#issuecomment-283963819
./configure
make -j$(nproc)
cd src/
gcc -static -o desktop-file-validate keyfileutils.o validate.o validator.o -lglib-2.0 -lintl
gcc -static -o update-desktop-database  update-desktop-database.o -lglib-2.0 -lintl
gcc -static -o desktop-file-install keyfileutils.o validate.o install.o  -lglib-2.0 -lintl
strip desktop-file-install desktop-file-validate update-desktop-database
cd ../..

# Build appstreamcli
apk add glib-static meson cmake libxml2-dev yaml-dev gobject-introspection-dev snowball-dev gperf
# Compile liblmdb from source as Alpine only ship it as a .so
wget https://git.openldap.org/openldap/openldap/-/archive/LMDB_0.9.29/openldap-LMDB_0.9.29.tar.gz
tar xf openldap-LMDB_0.9.29.tar.gz
cd openldap-LMDB_0.9.29/libraries/liblmdb
make liblmdb.a
sudo install -D -m 644 liblmdb.a /usr/local/lib/liblmdb.a
sudo install -D -m 644 lmdb.h /usr/local/include/lmdb.h
cd -
wget -c https://github.com/ximion/appstream/archive/v0.12.9.tar.gz
tar xf v0.12.9.tar.gz
cd appstream-0.12.9
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
apk add zlib-dev bzip2-dev # What happened to zlib-static?
wget https://www.libarchive.org/downloads/libarchive-3.3.2.tar.gz
tar xf libarchive-3.3.2.tar.gz
cd libarchive-3.3.2
./configure LDFLAGS='--static' --enable-bsdtar=static --disable-shared --with-zlib --without-bz2lib
make -j$(nproc)
gcc -static -o bsdtar tar/bsdtar-bsdtar.o tar/bsdtar-cmdline.o tar/bsdtar-creation_set.o tar/bsdtar-read.o tar/bsdtar-subst.o tar/bsdtar-util.o tar/bsdtar-write.o .libs/libarchive.a .libs/libarchive_fe.a /lib/libz.a
strip bsdtar
cd ..

#############################################
# Exit chroot and clean up
#############################################

exit
EOF
sudo umount miniroot/proc miniroot/sys miniroot/dev

#############################################
# Copy build artefacts out
#############################################


# Use the same architecture names as https://github.com/AppImage/AppImageKit/releases/
if [ "$ARCHITECTURE" = "x86" ] ; then export ARCHITECTURE=i686 ; fi

mkdir out/
sudo find miniroot/ -type f -executable -name 'mksquashfs' -exec cp {} out/mksquashfs-$ARCHITECTURE \; 2>/dev/null
sudo find miniroot/ -type f -executable -name 'unsquashfs' -exec cp {} out/unsquashfs-$ARCHITECTURE \; 2>/dev/null
sudo find miniroot/ -type f -executable -name 'bsdtar' -exec cp {} out/bsdtar-$ARCHITECTURE \; 2>/dev/null
sudo find miniroot/ -type f -executable -name 'desktop-file-install' -exec cp {} out/desktop-file-install-$ARCHITECTURE \; 2>/dev/null
sudo find miniroot/ -type f -executable -name 'desktop-file-validate' -exec cp {} out/desktop-file-validate-$ARCHITECTURE \; 2>/dev/null
sudo find miniroot/ -type f -executable -name 'update-desktop-database' -exec cp {} out/update-desktop-database-$ARCHITECTURE \; 2>/dev/null
sudo cp miniroot/appstream-0.12.9/prefix/bin/appstreamcli out/appstreamcli-$ARCHITECTURE
sudo find patchelf-*/ -type f -executable -name 'patchelf' -exec cp {} out/patchelf-$ARCHITECTURE \; 2>/dev/null
sudo rm -rf miniroot/ patchelf-*/
