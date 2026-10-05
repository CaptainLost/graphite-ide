# 0558. Ship the application's mark as icon resources exported from its SVG

Status: proposed
Date: 2026-10-05

## Context

The application's mark appears in two places. The taskbar, a system-drawn caption
and Explorer show the `GLFW_ICON` resource, which only an `.ico` can fill. The
custom title bar draws the mark itself at its left edge, without the disc behind
it.

The title bar first drew the mark as vectors, parsing `Graphite.svg` out of a
byte array that a script regenerated into a header. That left two copies of the
artwork reaching the screen by two routes: the `.ico`, exported by hand, and the
SVG, carried by the script. A change to the SVG updated the title bar and left
the taskbar on the old mark until someone remembered the export, and nothing
noticed when they did not.

## Decision

Ship every on-screen copy of the mark as an icon resource in the executable,
exported from `Graphite/resources/Graphite.svg`, which stays the artwork's only
source and is not read at build or run time:

- `GLFW_ICON` (`Graphite.ico`) is the whole icon, disc included.
- `GRAPHITE_MARK` (`GraphiteMark.ico`) is the mark without the disc, cropped to it.
  The title bar draws it at the size the icon holds an image for nearest to the
  height it wants (`Core::IconResourceSizes`), read through
  `Core::LoadIconResource` and shown as a texture, texel for pixel.

When the SVG changes, export both `.ico` files and commit them with it. No
script or build step generates them.

## Rationale

Windows takes an `.ico` for the taskbar and the executable regardless, so making
the title bar read an `.ico` too is the only arrangement with a single route to
the screen. Drawing only sizes the icon holds keeps the mark as sharp as a
vector would be: Windows' own scaling of an icon to a size it lacks loses alpha
at every edge, leaving no pixel of the mark fully opaque.

## Consequences

### Positive

- One mechanism shows the mark everywhere; the SVG parser, the generated header
  and its script are gone.
- The smallest sizes can be touched up by hand in the `.ico` and the title bar
  shows exactly that.

### Negative

- The export is still manual, and nothing checks that the two `.ico` files match
  the SVG. Forgetting one shows an old mark until it is noticed.
- The title bar's mark is a bitmap, so its height moves in steps of the sizes the
  `.ico` holds: a pixel or two off the caption's proportion at some scales, unless
  a frame of that exact size is exported into it.

### Neutral

- One texture is kept for each pixel height asked for, for the life of the process.

## Alternatives Considered

- **Parse the SVG at run time for the title bar** — what it replaced: sharp at any
  size, but a second route to the screen that drifted from the `.ico`.
- **Generate both `.ico` files from the SVG in a script or build step** — keeps
  them in step automatically, but adds tooling to maintain and rasterises small
  sizes that are better tuned by hand. Turned down for now.
- **Embed the SVG as an RCDATA resource** — drops the generated header but keeps
  two artworks in step by hand, the same drift as before.

## Related

- [0477](0477-draw-the-title-bar-in-the-application.md) — the custom title bar the mark is drawn in.
