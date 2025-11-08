# static-tools ![GitHub Actions](https://github.com/probonopd/static-tools/actions/workflows/build.yaml/badge.svg)

Building static binaries of some tools using an [Alpine Linux](https://alpinelinux.org/) chroot with [musl libc](https://www.musl-libc.org/):

* `bsdtar` (from libarchive)
* `mksquashfs`, `unsquashfs` (from squashfs-tools)
* `desktop-file-install`, `desktop-file-validate`, `update-desktop-database` (from desktop-file-utils)
* `appstreamcli` (from AppStream)

## AppStream CLI AppImages

In addition to static binaries, this repository also provides fully self-contained **AppImage** builds of `appstreamcli` targeting **AppStream 1.0**. These AppImages bundle everything including glibc and are available for 4 architectures (x86, x86_64, armhf, aarch64).

See [appstreamcli-appimage/README.md](appstreamcli-appimage/README.md) for details and usage instructions.

**Quick start:**
```bash
# Download for your architecture from releases
wget https://github.com/probonopd/static-tools/releases/download/continuous/appstreamcli-1.0.0-x86_64.AppImage
chmod +x appstreamcli-1.0.0-x86_64.AppImage
./appstreamcli-1.0.0-x86_64.AppImage --version
```

## Static AppImage runtime

__PLEASE NOTE: Do NOT add additional external dependencies or files. Everything shall be implemented in one file,  `runtime.c`.__

__PLEASE NOTE: Do NOT add a complicated "build system" (like autotools, CMake,...) other than the existing simple Makefile and bash.__

## Building locally

Binaries are provided on GitHub Releases. Should you prefer to build locally or on GitHub Codespaces, the following will build the contents of this repository in an Alpine container:

```
export ARCHITECTURE=x86_64
./chroot_build.sh
```

This whole process takes only a few seconds on GitHub Codespaces.

## How to build static binaries

* Build inside an Alpine Linux chroot (which gives us many dependencies from the system)
* Build verbose (e.g., `make -j$(nproc) VERBOSE=1`)
* Look for the gcc command that produces the executable (`-o name_of_the_executable`)
* Replace `gcc` with `gcc -static`
* Remove all `-W...`
* Remove `-lpthread`
* Some applications that use `./configure` can be configured like this: `./configure CFLAGS=-no-pie LDFLAGS=-static`- NOTE: `LDFLAGS=-static` only works for binaries, not for libraries: it will not result in having the build system provide a static library (`.a` archive)
