# static-tools [![Build Status](https://travis-ci.com/probonopd/static-tools.svg?branch=master)](https://travis-ci.com/probonopd/static-tools)

Building static binaries of some tools using an [Alpine Linux](https://alpinelinux.org/) chroot with [musl libc](https://www.musl-libc.org/):

* `bsdtar` (from libarchive)
* `mksquashfs`, `unsquashfs` (from squashfs-tools)
* `desktop-file-install`, `desktop-file-validate`, `update-desktop-database` (from desktop-file-utils)

This one I did not find out yet how to build static, but bundling musl libc is so much easier (and smaller) than bundling glibc:

* `appstreamcli` (from AppStream)
