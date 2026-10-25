# SteamForge

A desktop Steam account and authenticator manager for Windows and Linux.

SteamForge is in early development. Current builds contain the application
shell: system tray, single-instance launch, and an English and Russian
interface. Account features are not implemented yet.

## Planned features

- Multiple Steam accounts and independent sessions.
- Steam Guard setup, import, export, and code generation.
- Inventory browsing, trade offers, and mobile confirmations.
- QR sign-in assistance for the Steam client.
- Optional encrypted Google Drive storage and synchronization.

## Technology

C++23, Qt 6.11 (Widgets), CMake, and vcpkg. Release builds link Qt and the
application libraries statically and produce a single executable per platform.
The interface is available in English and Russian.

## Downloads

Each release provides two files:

- `SteamForge.exe` for Windows 10 (1809 or newer) and Windows 11 (x64);
- `SteamForge` for Linux (x64, glibc 2.28 or newer). Make it executable after
  downloading: `chmod +x SteamForge`.

## Building

Both platforms use CMake presets and a vcpkg checkout at the `builtin-baseline`
revision from `vcpkg.json`. The first build compiles Qt and takes a while.

### Windows

Requirements: Visual Studio 2026 with the C++ workload, and vcpkg
(`VCPKG_ROOT`, defaults to `C:/vcpkg`).

```bat
tools\build-windows.cmd release
```

The executable is written to `build\windows-release\SteamForge.exe`.

### Linux

Release builds run in the `manylinux_2_28` container (glibc 2.28, GCC 14), so
the executable runs on RHEL 8, Debian 11, Ubuntu 20.04, and newer.

```sh
docker run --rm -v "$PWD:/src" -w /src quay.io/pypa/manylinux_2_28_x86_64:2026.10.24-1 \
	bash -c "tools/ci/install-linux-build-dependencies.sh && tools/ci/build-linux.sh release"
```

The executable is written to `build/linux-release/SteamForge`. It uses the
system glibc, X11/XCB, xkbcommon, and D-Bus libraries; everything else is linked
in.

To add SteamForge with its icon to the application menu of the current user:

```sh
tools/linux/install-desktop-entry.sh build/linux-release/SteamForge
```

## License

[MIT](LICENSE).
