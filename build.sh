
#############################################
# Download and extract minimal Alpine system
#############################################

wget http://dl-cdn.alpinelinux.org/alpine/v3.10/releases/x86_64/alpine-minirootfs-3.10.2-x86_64.tar.gz
mkdir -p ./miniroot
cd ./miniroot
tar xf ../alpine-minirootfs-3.10.2-x86_64.tar.gz
cd -

#############################################
# Prepare chroot
#############################################

sudo mount -o bind /dev miniroot/dev
sudo mount -t proc none miniroot/proc
sudo mount -t sysfs none miniroot/sys
sudo cp -p /etc/resolv.conf miniroot/etc/
sudo chroot miniroot /bin/sh <<\EOF

#############################################
# Now inside chroot
#############################################

# Install build dependencies
apk update
apk add alpine-sdk bash util-linux strace file zlib-dev # OK: 198 MiB in 66 packages

# Build static squashfs-tools
wget -c http://deb.debian.org/debian/pool/main/s/squashfs-tools/squashfs-tools_4.4.orig.tar.gz
tar xf squashfs-tools_4.4.orig.tar.gz
cd squashfs-tools-4.4/squashfs-tools
make -j$(nproc)
ld -static mksquashfs.o read_fs.o action.o swap.o pseudo.o compressor.o sort.o progressbar.o read_file.o info.o restore.o process_fragments.o caches-queues-lists.o gzip_wrapper.o xattr.o read_xattrs.o /usr/lib/crt1.o /usr/lib/libc.a -lm -lz -o mksquashfs
ld -static unsquashfs.o unsquash-1.o unsquash-2.o unsquash-3.o unsquash-4.o unsquash-123.o unsquash-34.o swap.o compressor.o unsquashfs_info.o gzip_wrapper.o read_xattrs.o unsquashfs_xattr.o /usr/lib/crt1.o /usr/lib/libc.a -lm -lz -o unsquashfs
strip *squashfs
cd ..

# Build static desktop-file-utils
apk add glib-static glib-dev
wget -c https://www.freedesktop.org/software/desktop-file-utils/releases/desktop-file-utils-0.15.tar.gz
tar xf desktop-file-utils-0.15.tar.gz 
cd desktop-file-utils-0.15
./configure
make -j$(nproc)	
cd src/
ld -static -o desktop-file-validate keyfileutils.o validate.o validator.o -lglib-2.0 -lintl /usr/lib/crt1.o /usr/lib/libc.a
ld -static -o update-desktop-database  update-desktop-database.o -lglib-2.0 -lintl /usr/lib/crt1.o /usr/lib/libc.a
ld -static -o desktop-file-install keyfileutils.o validate.o install.o  -lglib-2.0 -lintl /usr/lib/crt1.o /usr/lib/libc.a
strip desktop-file-install desktop-file-validate update-desktop-database
cd -

# Build appstreamcli
# But entirely unclear how to make meson build a static binary
# but unlike with glibc it is rather easy to "bundle everything" with musl, result is 2.8 MB
apk add glib-static meson cmake libxml2-dev yaml-dev lmdb-dev gobject-introspection-dev snowball-dev gperf patchelf
wget -c https://github.com/ximion/appstream/archive/v0.12.9.tar.gz
tar xf v0.12.9.tar.gz
cd appstream-0.12.9
mkdir build && cd build
meson ..
ninja -v
libs=$(ldd  ./tools/appstreamcli | cut -d " " -f 3 | sort | uniq )
cp $libs tools/
cp /lib/ld-musl-x86_64.so.1 tools/
patchelf --set-rpath '$ORIGIN' tools/appstreamcli
strip ./tools/*
(cd tools/ ; tar cfvj ../appstreamcli.tar.gz * )
cd ../../

# Build static bsdtar
wget https://www.libarchive.org/downloads/libarchive-3.3.2.tar.gz
tar xf libarchive-3.3.2.tar.gz
cd libarchive-3.3.2
./configure LDFLAGS='--static' --enable-bsdtar=static --disable-shared --with-zlib
make -j$(nproc)
ld -static -o bsdtar tar/bsdtar-bsdtar.o tar/bsdtar-cmdline.o tar/bsdtar-creation_set.o tar/bsdtar-read.o tar/bsdtar-subst.o tar/bsdtar-util.o tar/bsdtar-write.o /usr/lib/crt1.o .libs/libarchive.a .libs/libarchive_fe.a /usr/lib/libc.a  /lib/libz.a 
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

mkdir -p out/
sudo find miniroot/ -type f -executable -name '*squashfs' -exec cp {} out/ \; 2>/dev/null
sudo find miniroot/ -type f -executable -name 'bsdtar' -exec cp {} out/ \; 2>/dev/null
sudo find miniroot/ -type f -executable -name 'desktop-file-install' -exec cp {} out/ \; 2>/dev/null
sudo find miniroot/ -type f -executable -name 'desktop-file-validate' -exec cp {} out/ \; 2>/dev/null
sudo find miniroot/ -type f -executable -name 'update-desktop-database' -exec cp {} out/ \; 2>/dev/null
sudo find miniroot/ -type f -name 'appstreamcli.tar.gz' -exec cp {} out/ \; 2>/dev/null
