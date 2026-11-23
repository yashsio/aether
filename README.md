# Aether

A minimal, fast, **Wayland-native** image viewer in C++17 — built to be small,
hermetic, and dependency-light.

Aether draws its own pixels straight into a shared-memory (`wl_shm`) buffer
using a minimal embedded rasterizer and hands that buffer to the running
Wayland compositor (no GTK/Qt, no Cairo, no Pango). Every image codec and the
font rasterizer are vendored (stb).

---

## Features

- **Formats** (decode): PNG, JPEG, BMP, TGA, GIF, HDR, PSD, PNM (PPM/PGM/PBM),
  PIC, WebP (default build), SVG (rasterized with nanosvg). **Encode** (for
  rotation save): PNG, JPEG, BMP, TGA, WebP (lossless). Rotating an SVG writes
  a `<name>.rotated.png` sibling rather than touching the vector source.
- **Navigation**: browse a folder with the arrow keys (wraps around), natural
  sort (`img2.png` before `img10.png`).
- **View**: fit-to-window and 1:1, zoom in/out, free pan by dragging.
- **Rotate** left/right in 90° steps, optionally saving the rotation back to
  disk losslessly.
- **Touchpad pinch-to-zoom** (anchored under your fingers) and Ctrl+wheel zoom
  (anchored on the image center).
- **Metadata overlay** (toggle filename, path, size, resolution, modified time).
- **Help overlay** (toggle display all the keybinds)
- **Drag & drop** an image onto the window to open it, and delete the current
  image with Delete/Backspace.

---

## Requirements

- A C++17 compiler (GCC or Clang) and **CMake ≥ 3.16**
- **Wayland** client libraries:
  - `wayland-client` and `wayland-cursor` (both from the `wayland` package)
  - `wayland-scanner` + `wayland-protocols` (build time only)
  - `libxkbcommon` (keyboard layout handling)
  - `libffi` (transitive dependency of `wayland-client`)

The application links only these (plus a static libwebp build). Everything
above the platform layer — image decoding/encoding, mipmap generation, font
rasterization — is compiled in.

WebP is enabled by default: CMake fetches a pinned libwebp release
(v1.6.0, SHA-256 verified) and builds it statically. Pass
`-DAETHER_WEBP=OFF` to omit it, or `-DAETHER_WEBP_SYSTEM=ON` to link a
system libwebp through `pkg-config` instead (what the Nix package does).
SVG support is always compiled in via the vendored nanosvg.

---

## Build Instructions

### On NixOS

The repo ships a flake providing the exact toolchain (GCC, CMake,
`pkg-config`, Wayland, `wayland-scanner`, `wayland-protocols`,
`libxkbcommon`, `libffi`, `clangd`, `gdb`):
   
    nix develop                          # enter the dev shell
    cmake -B build                       # configure
    cmake --build build -j"$(nproc)"     # compile
    ./build/aether path/to/photo.jpg     # run


### On other distributions

Install the toolchain and Wayland development packages:

    sudo apt install build-essential cmake pkg-config \
        libwayland-dev libxkbcommon-dev libffi-dev     # Debian/Ubuntu
    # or
    sudo dnf install gcc-c++ cmake pkgconf \
    wayland-devel wayland-protocols-devel libxkbcommon-devel libffi-devel  # Fedora
    # or
    sudo pacman -S base-devel cmake pkgconf \
    wayland wayland-protocols libxkbcommon libffi  # Arch

Then:

    cmake -B build
    cmake --build build -j"$(nproc)"
    ./build/aether path/to/photo.jpg

You can also drag an image file onto a running window.

### Tests

    ctest --test-dir build --output-on-failure

The test suite (`tests/test_ops.cpp`) covers the non-platform logic: image
rotation, mip generation, codec round-trips, natural sort, saved-file
round-trips, and the renderer's opacity/partial-redraw invariants.

---

## Usage

    aether <image>

Launches into the folder containing `<image>`, so the arrow keys navigate the
other images in that directory. A rotation operation is saved with `Ctrl+S` rewrites the file in place.

Just press the `?` key or `/` for the help overlay at runtime.

### SVG notes

SVG is rasterized once at its natural size (capped at 4096 px on the long
edge), then treated like any other bitmap, so it softens when zoomed past 1:1.
nanosvg covers shapes, paths, transforms, gradients and clip paths, but not
text, filters, CSS selectors or external references. Because there is no SVG
encoder, `Ctrl+S` after rotating an SVG writes `photo.rotated.png` next to
`photo.svg` and leaves the original untouched.

---

## License

Third-party code under `third_party/stb/` is public domain (see the headers),
and `third_party/nanosvg/` is zlib-licensed (see its headers).
The rest of the project is licensed under [GPL-3.0](LICENSE)
