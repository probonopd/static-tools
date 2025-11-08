# AppStream CLI AppImage

Fully self-contained AppImage builds of `appstreamcli` targeting AppStream 1.0.

## About

This directory contains build scripts for creating AppImage bundles of the `appstreamcli` tool from [AppStream](https://github.com/ximion/appstream) 1.0. These AppImages are fully self-contained and bundle all dependencies, including glibc and all required libraries, using go-appimage's `appimage -s deploy` functionality.

## Features

- **Fully Self-Contained**: Bundles everything including glibc and all libraries
- **Multi-Architecture**: Supports 4 architectures:
  - `x86` - 32-bit Intel/AMD
  - `x86_64` - 64-bit Intel/AMD
  - `armhf` - 32-bit ARM (ARMv7)
  - `aarch64` - 64-bit ARM (ARMv8/ARM64)
- **AppStream 1.0**: Targets AppStream 1.0.0 (latest stable 1.0.x release)
- **Automated Builds**: GitHub Actions workflow builds and uploads to releases

## Usage

### Download

Download the AppImage for your architecture from the [continuous release](https://github.com/probonopd/static-tools/releases/tag/continuous):

```bash
# For 64-bit Intel/AMD systems
wget https://github.com/probonopd/static-tools/releases/download/continuous/appstreamcli-1.0.0-x86_64.AppImage
chmod +x appstreamcli-1.0.0-x86_64.AppImage

# For 32-bit Intel/AMD systems  
wget https://github.com/probonopd/static-tools/releases/download/continuous/appstreamcli-1.0.0-x86.AppImage
chmod +x appstreamcli-1.0.0-x86.AppImage

# For 64-bit ARM systems
wget https://github.com/probonopd/static-tools/releases/download/continuous/appstreamcli-1.0.0-aarch64.AppImage
chmod +x appstreamcli-1.0.0-aarch64.AppImage

# For 32-bit ARM systems
wget https://github.com/probonopd/static-tools/releases/download/continuous/appstreamcli-1.0.0-armhf.AppImage
chmod +x appstreamcli-1.0.0-armhf.AppImage
```

### Run

Simply execute the AppImage:

```bash
./appstreamcli-1.0.0-x86_64.AppImage --version
./appstreamcli-1.0.0-x86_64.AppImage --help
./appstreamcli-1.0.0-x86_64.AppImage validate /path/to/metainfo.xml
```

## Building Locally

You can build the AppImages locally using Docker:

```bash
# Clone the repository
git clone https://github.com/probonopd/static-tools.git
cd static-tools/appstreamcli-appimage

# Build for x86_64
docker run --rm --platform linux/amd64 \
  -v "$PWD:/work" \
  -w /work \
  -e ARCH=x86_64 \
  -e APPSTREAM_VERSION=1.0.0 \
  debian:bookworm \
  bash -c "apt-get update && apt-get install -y wget file && bash build-appstreamcli-appimage.sh"

# Build for other architectures by changing --platform and ARCH:
# --platform linux/386 for x86
# --platform linux/arm64 for aarch64
# --platform linux/arm/v7 for armhf
```

## How It Works

The build process:

1. **Build AppStream**: Compiles AppStream 1.0.0 from source with all dependencies
2. **Create AppDir**: Prepares an AppDir directory structure with:
   - The `appstreamcli` binary
   - A desktop file
   - An icon
   - An AppRun script for proper environment setup
3. **Bundle Dependencies**: Uses go-appimage's `appimagetool -s deploy` to:
   - Analyze all library dependencies
   - Bundle glibc and all required libraries
   - Create a fully self-contained AppImage
4. **Test**: Verifies the AppImage works by running `--version`

## Automated Builds

GitHub Actions automatically builds AppImages for all 4 architectures on every push to the master branch. The workflow:

1. Sets up QEMU for multi-architecture support
2. Builds each architecture in a Debian Docker container
3. Tests the resulting AppImages
4. Uploads artifacts
5. Publishes to the "continuous" GitHub Release

## Technical Details

- **Base System**: Debian Bookworm (for modern libraries while maintaining compatibility)
- **Build Tool**: Meson (AppStream's build system)
- **Bundling Tool**: go-appimage's appimagetool with `-s deploy` flag
- **AppStream Version**: 1.0.0 (configurable via `APPSTREAM_VERSION` environment variable)

## Troubleshooting

### AppImage doesn't run

Make sure it's executable:
```bash
chmod +x appstreamcli-*.AppImage
```

### Architecture mismatch

Ensure you downloaded the correct architecture for your system:
```bash
uname -m  # Check your architecture
# x86_64 → use x86_64 AppImage
# i686 or i386 → use x86 AppImage
# aarch64 → use aarch64 AppImage
# armv7l → use armhf AppImage
```

### Missing libraries

The AppImages should be fully self-contained. If you encounter library issues, please open an issue on GitHub.

## Why AppImages?

AppImages provide several advantages:

- **No Installation Required**: Just download and run
- **No Root Required**: Can be used in restricted environments
- **Portable**: Works across different Linux distributions
- **Self-Contained**: No dependency conflicts
- **Backwards Compatible**: Bundle newer libraries on older systems

## Contributing

Contributions are welcome! Please open issues or pull requests on GitHub.

## License

This build infrastructure is provided under the same license as the static-tools repository.

AppStream itself is licensed under the LGPL-2.1+ and GPL-2.0+ licenses.

## Related Projects

- [AppStream](https://github.com/ximion/appstream) - The AppStream project
- [go-appimage](https://github.com/probonopd/go-appimage) - Tools for creating AppImages
- [static-tools](https://github.com/probonopd/static-tools) - Collection of static tools
