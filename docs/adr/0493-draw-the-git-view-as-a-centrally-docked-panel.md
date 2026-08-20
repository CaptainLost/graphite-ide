# 0493. Draw the git view as a centrally docked panel

Status: proposed
Date: 2026-08-20

## Context

The git view needed a home: refs down one side, and beside them a history
whose commits expand in place to show the files each one changed, over a
diff of whichever file is selected along the bottom. Two places in the
running application already hold a screen area that widgets are drawn
into — `DocumentWindows`, the tabbed area that shows the files a person has
open, and `Core::PanelManager`, which docks named panels (the explorer, the
console, search results) around the edges and centre of the window.

`DocumentWindows` is keyed by `std::filesystem::path`: every tab it holds
corresponds to a file that exists on disk, opened through `Core::DocumentSet`.
The git view is not that — it has no single file backing it, and what it shows
changes with which ref and which commit is selected, not with which file was
opened. Nothing in `DocumentWindows` today models a tab that isn't a document.

`Core::PanelManager` places no such requirement on what it holds. A panel is a
`Core::Panel` with an id, a title, and an `OnImGuiRender` — it can dock
anywhere in the layout, including the centre area alongside document tabs,
which is where a git view naturally belongs since it wants roughly the same
amount of screen space as an editor.

## Decision

Implement the git view as `Graphite::GitView : public Core::Panel`, added to
`Core::PanelManager` and docked into `m_Layout.Center` beside the settings
panel, rather than as a tab inside `DocumentWindows`.

## Rationale

A panel fits what `GitView` actually is — a view keyed by selection state
(ref, commit, file), not by a path on disk — without requiring any change to
a system, `DocumentWindows`, that is built around the opposite assumption.
The panel manager already supports docking into the centre, so the git view
gets a document-sized home for free.

## Consequences

### Positive

- No change to `DocumentWindows` or `Core::DocumentSet`; both keep their
  single, simple meaning of "a tab is a file that exists on disk."
- The git view participates in the same show/hide, layout, and command
  infrastructure every other panel already uses — `PanelManager::ResetVisibility`
  and the panels menu — with no bespoke plumbing.

### Negative

- The git view cannot be arranged the way a document tab can be: it cannot sit
  in the same tab strip as an open file, cannot be dragged out as its own
  window through `DocumentWindows`' mechanism, and does not participate in
  "close all documents" or similar document-scoped operations. A person who
  wants the commit graph and a file open side by side gets two differently
  shaped panes rather than two document tabs.

### Neutral

- The panel is constructed hidden (`Core::Panel`'s `visible` argument is
  `false`) and reveals itself through the panels group `PanelManager::Add`
  registers it into, which the View menu renders, so it adds nothing to the
  default layout until asked for.

## Alternatives Considered

**Rework `DocumentWindows` around virtual documents.** Generalise the tab
system so a tab can be backed by something other than a filesystem path — a
commit, a ref, a diff — and open the git view as such a tab. This would let
the git view sit in the same strip as open files and inherit tab-level
behaviour (drag-out, close-all) directly.

Not taken: it is a larger decision than this work needs. `DocumentWindows` and
`Core::DocumentSet` are built end-to-end around the assumption that a document
is a file, from how a tab is identified to how its lifecycle is tracked; making
that generic touches both without doing anything for the reader beyond a
different set of trade-offs than the panel gives today. This plan builds one
view; deciding whether a second and third virtual-document consumer justify
that rework is a decision for when one exists.

## Related

- The commit list, diff widgets, and colour palette this panel assembles come
  from the same plan and live in `Graphite/src/Git/`.
