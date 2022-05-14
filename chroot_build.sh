#!/bin/sh

set -ex

#############################################
# Download and extract minimal Alpine system
#############################################

wget http://dl-cdn.alpinelinux.org/alpine/v3.15/releases/$ARCHITECTURE/alpine-minirootfs-3.15.4-$ARCHITECTURE.tar.gz
sudo rm -rf ./miniroot  true # Clean up from previous runs
mkdir -p ./miniroot
cd ./miniroot
sudo tar xf ../alpine-minirootfs-*-$ARCHITECTURE.tar.gz
cd -

#############################################
# Prepare chroot
#############################################

sudo cp -r ./src miniroot/src

sudo mount -o bind /dev miniroot/dev
sudo mount -t proc none miniroot/proc
sudo mount -t sysfs none miniroot/sys
sudo cp -p /etc/resolv.conf miniroot/etc/

#############################################
# Run build.sh in chroot
#############################################

sudo chroot miniroot /bin/sh -ex <build.sh

#############################################
# Clean up chroot
#############################################

sudo umount miniroot/proc miniroot/sys miniroot/dev

#############################################
# Copy build artefacts out
#############################################

# Use the same architecture names as https://github.com/AppImage/AppImageKit/releases/
if [ "$ARCHITECTURE" = "x86" ] ; then ARCHITECTURE=i686 ; fi

mkdir out/
sudo find miniroot/ -type f -executable -name 'runtime-fuse2' -exec cp {} out/runtime-fuse2-$ARCHITECTURE \;
sudo find miniroot/ -type f -executable -name 'runtime-fuse3' -exec cp {} out/runtime-fuse3-$ARCHITECTURE \;
sudo find miniroot/ -type f -executable -name 'patchelf' -exec cp {} out/patchelf-$ARCHITECTURE \;
sudo find miniroot/ -type f -executable -name 'zsyncmake' -exec cp {} out/zsyncmake-$ARCHITECTURE \;
sudo find miniroot/ -type f -executable -name 'mksquashfs' -exec cp {} out/mksquashfs-$ARCHITECTURE \;
sudo find miniroot/ -type f -executable -name 'unsquashfs' -exec cp {} out/unsquashfs-$ARCHITECTURE \;
sudo find miniroot/ -type f -executable -name 'bsdtar' -exec cp {} out/bsdtar-$ARCHITECTURE \;
sudo find miniroot/ -type f -executable -name 'desktop-file-install' -exec cp {} out/desktop-file-install-$ARCHITECTURE \;
sudo find miniroot/ -type f -executable -name 'desktop-file-validate' -exec cp {} out/desktop-file-validate-$ARCHITECTURE \;
sudo find miniroot/ -type f -executable -name 'update-desktop-database' -exec cp {} out/update-desktop-database-$ARCHITECTURE \;
sudo cp miniroot/appstream-0.12.9/prefix/bin/appstreamcli out/appstreamcli-$ARCHITECTURE
sudo rm -rf miniroot/
