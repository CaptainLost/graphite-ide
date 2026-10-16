# 0576. Draw a loading indicator through a Core facade over imspinner

Status: proposed
Date: 2026-10-11

## Context

The file tree's filter reads the whole folder in the background before it can
show anything ([0575](0575-filter-the-file-tree-against-a-listing-of-the-whole-folder-read-in-the-background.md)).
In a large folder that takes long enough to be seen, and the view said so with
a static "Searching..." line, which looks unfinished next to the animated
hovers and toasts elsewhere in the interface. Nothing in the codebase drew a
loading indicator.

[imspinner](https://github.com/dalerank/imspinner) is a header-only collection
of ImGui spinners, MIT-licensed, with a family of animated text loaders. It
follows ImGui's font API across the 1.92 change, which the ImGui in `vendor/`
(1.93 WIP) needs.

## Decision

- Add imspinner as a submodule in `vendor/imspinner`, header-only, reached
  through an `IncludeDir` entry and Core's `externalincludedirs`
  ([0003](0003-vendor-dependencies-as-git-submodules-built-by-premake.md)).
- Include it in one file only, `Core/ImGui/LoadingState.cpp`. The rest of the
  application calls `Core::LoadingState(text)`, which owns the choice of
  loader, its speed and its colours, the way `Core::Animation` owns the
  application's tweens ([0516](0516-animate-the-ui-through-a-core-facade-over-imanim.md)).
- Draw the text with `SpinnerTextShine`, in the middle of what is left of the
  current window: the text in the theme's text colour, with a band of that
  colour sweeping across it once every two seconds and the letters under the
  band in the window's background colour. A text wider than the window is drawn
  smaller to fit.
- Ask the frame loop for the next frame on every frame the loader is drawn,
  since the loop otherwise sleeps ([0530](0530-draw-a-frame-only-when-there-is-reason-to.md)).
- Show it only once the wait has lasted 200 ms. The facade keeps each loader's
  wait itself, draws nothing before then and sets a wake-up for the moment the
  loader is due, so a folder read in a few milliseconds never flashes a loader
  for one frame. A one-line loader that takes the place of what the user just
  clicked, such as the history's status line after Load 500 more, passes
  `WhileLoaderWaits::ShowText`: its text shows at once, dim, and the spinner
  joins it in front once the wait is over, the text staying where it is.

## Rationale

A header-only library behind one function adds no project to the build and no
compiled code outside the file that uses it. The facade keeps every loading
indicator the same and gives a later "reduce motion" setting one place to take
effect, as it does for the tweens.

## Consequences

### Positive

- A view that waits says so with motion that matches the rest of the
  interface, in the theme's colours.
- Replacing the loader with another of imspinner's, or with a hand-drawn one,
  touches one file.

### Negative

- A new submodule, so a fresh clone or worktree has one more repository to
  fetch.
- While the loader shows, the application draws at the display's rate instead
  of sleeping.
- imspinner's headers are large, about 250 KB for the two the facade includes,
  and compile into that one file.

### Neutral

- imspinner's own warnings are not checked: its directory is an external
  include, so Core's fatal warnings do not reach it.

## Alternatives Considered

- **Animate the text by hand with ImAnim.** No new dependency, but every
  loader effect would be written and tuned here, and the text loaders imspinner
  already has are exactly the effects wanted.
- **Keep the static "Searching..." line.** Nothing to add, but it reads as
  frozen in a folder large enough for the wait to be noticed.

## Related

- [0003](0003-vendor-dependencies-as-git-submodules-built-by-premake.md) — how a
  dependency enters the repository.
- [0516](0516-animate-the-ui-through-a-core-facade-over-imanim.md) — the facade
  this one mirrors.
- [0530](0530-draw-a-frame-only-when-there-is-reason-to.md) — why the loader asks
  for its frames.
- [0575](0575-filter-the-file-tree-against-a-listing-of-the-whole-folder-read-in-the-background.md) —
  the wait it is shown for.
