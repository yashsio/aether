# nanosvg

Vendored copy of [memononen/nanosvg](https://github.com/memononen/nanosvg),
commit `239e102ec2c691f2902e20ace2ed36ee4a35cfe6`, files:

- `nanosvg.h` — SVG parser
- `nanosvgrast.h` — scanline rasterizer

Used by `src/image/codec_svg.cpp`. Both headers are zlib-licensed; the full
license text is at the top of each file. nanosvg supports shapes, paths,
transforms, gradients and clip paths, but not text, filters, CSS selectors or
external references.
