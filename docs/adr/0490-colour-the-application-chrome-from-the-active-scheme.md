# 0490. Colour the application chrome from the active scheme

Status: proposed
Date: 2026-08-20

## Context

`Core::Theme::ApplyDark()` painted the whole ImGui style from a table of
file-scope colour constants — background, surface, accent, text, border and
their hover and active variants — baked into `Theme.cpp` and applied once at
startup. `Graphite/src/Theme` already has a colour scheme model: a scheme is
a set of named colours read from a file, with `ChromeColor` naming eight
chrome slots and `kDarkChrome` in `BuiltInSchemes.cpp` giving each a value.
Nothing connected the two tables. `kDarkChrome`'s comment already says it
transcribes `Core::Theme`'s own default palette "once" — an admission that
the two constant tables were kept in sync by a person reading one and typing
the other, with no compiler or test standing between them.

[0002](0002-keep-core-free-of-ide-knowledge.md) holds that Core does not
depend on Graphite: no header, type, function or build setting in Core may
refer to anything Graphite defines. A scheme — a named, user-editable set of
colours loaded from a file — is entirely a Graphite concept. `ApplyDark`
choosing its own colours meant an active scheme could never reach the chrome
it was supposed to describe without either teaching Core what a scheme is or
leaving the transcription problem in place indefinitely.

## Decision

`Core::Theme::Apply` takes a `ChromePalette` — eight colours: `Background`,
`Surface`, `SurfaceHovered`, `SurfaceActive`, `Accent`, `Text`,
`TextDisabled`, `Border` — instead of reading file-scope constants.
`Core::Theme::DefaultPalette()` returns the values `ApplyDark` used to hold,
so an application built on Core alone, with no scheme of its own, still
starts up looking exactly as it did before this change. `GetBorderColor()`
stays, now answering `DefaultPalette().Border`, so the accessor
`Core::Application` uses when building a window spec cannot drift from the
palette `Apply` paints with.

`ChromePalette` names eight colours, not one field per `ImGuiCol_`. Eight is
what `Apply`'s body actually distinguishes — several `ImGuiCol_` entries
share one source colour already (`FrameBg`, `TitleBg` and `MenuBarBg` all
paint from `Surface`, for instance) — and a caller that had to name fifty
ImGui enumerators to supply eight colours would be coupled to ImGui's own
vocabulary, which is free to add, rename or retire entries between versions.
Core still owns every `ImGuiCol_` assignment; the palette only supplies the
eight colours those assignments are built from.

Core gains no knowledge of what a scheme is. It receives eight colours and
knows how to paint an ImGui style with them; ADR 0002 is unchanged by this
decision; it is what makes this decision possible in the first place.

Amended by [0613](0613-draw-the-chrome-darker-than-the-documents-and-diffs-it-frames.md):
`ChromePalette` has a ninth colour, `Content`; `DefaultPalette()` has a darker
`Background` and `Border`; and `MenuBarBg` paints from `Background` instead of
`Surface`.

## Rationale

A palette of eight named colours is the smallest interface that lets an
application substitute its own values without exposing ImGui's colour
vocabulary to whatever supplies them. Any narrower interface — an accent
colour alone, say — could not reproduce what `ApplyDark` already painted;
any wider one either duplicates ImGui's enumerators in Core's public API or
asks Core to interpret a scheme, both of which put IDE-shaped knowledge back
on Core's side of the boundary ADR 0002 draws.

## Consequences

### Positive

- An application built on Core alone still starts up with exactly the chrome
  `ApplyDark` produced, because `DefaultPalette()` is that table, moved
  rather than changed.
- `GetBorderColor()` and `Apply` now read from the same values by
  construction — a change to one cannot leave the other stale, which the
  previous two independently-maintained call sites could.
- `kDarkChrome` in `BuiltInSchemes.cpp` can stop being a transcription of a
  table it cannot see change: once Graphite's active scheme is threaded
  through to `Apply`, `kDarkChrome` becomes the value the application
  actually runs on, and Core's `DefaultPalette()` becomes only the fallback
  an application with no scheme of its own gets. The comment in
  `kDarkChrome` describing itself as a one-time transcription describes a
  problem this change is what ends.

### Negative

- `Theme::Apply` takes a struct instead of reading globals, so every caller
  now has to have a `ChromePalette` in hand before it can paint a style;
  today that is one call site, and it supplies `DefaultPalette()` unchanged.
- The eight-colour palette is coarser than `ImGuiCol_`'s full set. A scheme
  author who wants, say, a `FrameBg` that differs from `TitleBg` — both
  currently painted from `Surface` — cannot express that through
  `ChromePalette` without `Apply` growing another field, which was true of
  `ApplyDark`'s constants already and is not a new limitation, but is now a
  limitation of a documented public interface rather than of a file-scope
  table nobody outside `Theme.cpp` could see.

### Neutral

- The style metrics `Apply` sets — rounding, border sizes, padding, spacing,
  `WindowMenuButtonPosition` — are unchanged and are not part of
  `ChromePalette`. They are not colours, and nothing so far has asked a
  scheme to vary them.

## Alternatives Considered

### A scheme file addressing every `ImGuiCol_` under a `ui.*` key

Let a scheme name each ImGui colour directly — `ui.WindowBg`, `ui.FrameBg`,
`ui.TitleBgActive` and so on — so no palette struct sits between a scheme
file and the style ImGui ends up painting. Rejected on two counts: it puts
ImGui's own enumerator names into the file format a person hand-edits, which
outlives any particular version of ImGui and breaks if the enumerator it
names is ever renamed or removed; and it hands a scheme author roughly fifty
keys to fill in for a change eight colours already describe completely,
most of them repeating the same value under a different key.

### Have Core read the active scheme itself

Give `Theme` a way to ask Graphite's `ActiveScheme` for its chrome colours
directly, so no caller has to fetch a palette and hand it over. Rejected
outright under [0002](0002-keep-core-free-of-ide-knowledge.md): a scheme is
a Graphite concept, and Core reaching for one — even by an abstract
interface — is the inverted dependency that record already forbids.

## Related

- [0002](0002-keep-core-free-of-ide-knowledge.md) — the boundary this
  decision stays inside of; a palette of colours crosses it, a scheme does
  not.
