# Aether

A minimal, fast, cross-platform image viewer in **C++17 + Qt Widgets**. 

Still needed a lot of optimizations so not ideal for daily usage but i am working on that.

---

## Requirements

- **Qt 6** (recommended) or **Qt 5** (`QtWidgets`), any version ≥ 5.15
- C++17 compiler (GCC/Clang)
- CMake ≥ 3.16
- On Linux with **Wayland**, `qtwayland` so the window gets the Wayland platform
  plugin.

---

## Build Instructions

### On NixOS

The repo ships a flake that provides the whole toolchain pinned to
`nixos-unstable` (GCC, CMake, Ninja, `pkg-config`, `qt6.qtbase`,
`qt6.qtwayland`, `clangd`, `gdb`), with `QT_PLUGIN_PATH` pre-configured so the
Wayland/X11 platform plugins are found:

    nix develop                                 # enter the dev shell
    cmake -B build -G Ninja                     # configure
    cmake --build build                         # compile
    ./build/aether path/to/photo.jpg            # run

If you use `direnv`, `nix develop` runs automatically when you enter the
directory (the repo contains an `.envrc`).

### On other distributions

Choose the command to install dependencies as per your distro's package manager.

    sudo apt install qt6-base-dev qt6-wayland cmake g++     # Debian/Ubuntu
    # or
    sudo dnf install qt6-qtbase-devel cmake gcc-c++         # Fedora 
    # or
    sudo pacman -S qt6-base cmake gcc                       # Arch Linux
 
Build & Run via the following commands:

    cmake -B build -G Ninja
    cmake --build build
    ./build/aether path/to/photo.jpg

`CMakeLists.txt` prefers Qt 6 and falls back to Qt 5 if only that
is installed.

---
