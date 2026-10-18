# 0590. Compile the bundled fonts into the executable

Status: proposed
Date: 2026-10-15

## Context

The text face, its bold face and the icon font were `.ttf` files copied next to the executable by a post-build step and opened by path at startup. A build run from anywhere else, or a copy of the executable moved alone, started with ProggyClean and no icons, and every load path carried its own "file missing" fallback and warning.

## Decision

The three bundled faces (JetBrains Mono Regular and Bold, Material Design Icons) are compiled into `Core` as ImGui-compressed byte arrays and loaded with `AddFontFromMemoryCompressedTTF`.

- The sources of truth stay the `.ttf` files in `vendor/fonts`.
- `scripts/New-FontData.ps1` regenerates `Core/src/Core/ImGui/Fonts/*.cpp` from them with ImGui's own `binary_to_compressed_c`. The generated files are committed and not edited by hand. Replacing a font means running the script.
- `Core/ImGui/EmbeddedFonts.h` is the only way to reach the data. The atlas is told it does not own it.
- The system symbol font (`seguisym.ttf`) is still read from the system fonts directory: it is not ours to bundle.

Amended by [0615](0615-draw-the-interface-in-inter-and-code-in-jetbrains-mono.md):
the bundled faces are now Inter Regular, Inter SemiBold and JetBrains Mono Regular,
with Material Design Icons. JetBrains Mono Bold is no longer embedded.

## Rationale

A font that is part of the executable cannot be missing, so the fallback paths and their warnings go. Compressing keeps the generated sources and the binary far smaller than raw bytes would (the icon font is 1.3 MB). Generating ahead of time, rather than in the build, keeps the build free of a tool-building step.

## Consequences

### Positive

- The executable runs from any directory; the fonts cannot be left behind.
- No post-build copy of fonts, no missing-file handling.

### Negative

- About 3.8 MB of generated source in the repository, which changes whenever a font does.
- A font update is two steps: replace the `.ttf`, run the script.
- ImGui decompresses the data at startup, a small cost on every launch.

### Neutral

- `JetBrainsMono-Italic.ttf` is not used by the code and is not embedded.

## Alternatives Considered

- **A Windows resource (`RCDATA`).** No generated source, but it ties the engine to the Windows resource compiler and a static library cannot carry its own resources to the executable.
- **Uncompressed byte arrays.** Simpler, but about 2.3 times the source size for the same fonts.
- **Generating at build time.** No committed generated files, but every build needs the tool and a compiler step before `Core` compiles.

## Related

- [0550](0550-lay-out-sources-by-feature-in-whole-word-folders-mirrored-by-the-tests.md)
