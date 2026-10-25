# 0639. Add stb as a dependency for decoding and scaling images

Status: proposed
Date: 2026-10-25

## Context

Graphite is about to open image files in a tab. Nothing in the repository
decodes a png, jpeg, gif or bmp, and nothing scales a bitmap. The viewer needs
both: decoded pixels to upload as a texture, and a way to bring an image larger
than the GPU's texture limit down to it.

## Decision

Adopt [stb](https://github.com/nothings/stb) as a submodule in `vendor/stb`,
pinned to the commit it was added at (`003501f6`). `vendor/premake/stb.lua`
builds one C file, `vendor/premake/stb/StbImplementation.c`, into a static
library, `stb`, as `miniz` is. The file holds the bodies of `stb_image.h`, with
only png, jpeg, gif and bmp enabled and no file I/O, and of
`stb_image_resize.h`. Core includes the two headers as external includes and
never defines an implementation macro; the executables and test projects link
`stb`.

The resizer is `deprecated/stb_image_resize.h`, not `stb_image_resize2.h`: stb
moved the second version into a repository of its own and left only the first
in the tree this submodule tracks. It has `stbir_resize_uint8_srgb`, which
scales in linear light and keeps the alpha channel straight, and it is the
header the include path `vendor/stb/deprecated` exists for.

`Core/Image` is the only code that calls stb. Its API speaks in
`RgbaImage` and `Result`, so the rest of the codebase never sees an stb type.

## Rationale

stb is two single-file headers, public-domain, and covers every format the
viewer lists. Core builds with fatal warnings at level High, which stb's code
does not satisfy; compiling the implementation in the vendor library, where
warnings are off, keeps its warnings out of Core, and including the headers as
external includes keeps them out of everything else.

## Consequences

### Positive

- Four formats and a resizer for the price of one small static library.
- No stb type or macro leaks past `Core/Image`.

### Negative

- The resizer is the deprecated first version: it receives no fixes, and a move
  to the second version is a separate change once that header is vendored.
- stb decodes untrusted bytes in-process. The decoder refuses images past a
  pixel limit before decoding them, but a malformed file reaches stb's parsers.

### Neutral

## Alternatives Considered

- **WIC (Windows Imaging Component).** Already on every Windows machine and
  handles more formats. It ties decoding to Windows COM and cannot be tested
  the way the rest of Core is, headless and with bytes in memory.
- **libpng, libjpeg and giflib separately.** Better maintained per format, but
  three build recipes and three APIs for what four stb calls do here.

## Related

- ADR 0518 (ImPlot, another header-heavy vendor dependency)
