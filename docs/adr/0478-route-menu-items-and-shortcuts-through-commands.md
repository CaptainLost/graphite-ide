# 0478. Route menu items and shortcuts through commands

Status: proposed
Date: 2026-08-12

## Context

Every application shortcut was an `ImGui::Shortcut` call inlined in
`GraphiteLayer::OnUIRender`, and the text shown beside the matching menu item
was a separate string literal — `"Ctrl+="` next to the `Shortcut` call that
actually bound `Ctrl+=`. Nothing kept the two in agreement; a shortcut changed
in one place and forgotten in the other would drift silently. Neither could be
rebound — the chord was the call site. And the state those menus changed
(editor font size, word wrap, the git-ignored display mode) lived in
`GraphiteLayer` itself only because the menus that changed it did, not because
the layer had any other reason to own it.

## Decision

A menu item or a key binding reaches behaviour only through a `core::Command`
id, registered in `core::CommandRegistry` by whoever owns the state that
command changes — not by the layer that draws the menu. `core::Keymap` owns
the chords, in two layers, defaults and user overrides; a menu displays
`Keymap::For(id)->primary.ToString()` rather than a literal, so the label and
the binding cannot disagree. `CommandRegistry::Run` refuses a command its own
`enabled` predicate disables, so a menu item and its shortcut are greyed out
from the same check.

## Rationale

Both problems — the label/binding split and the layer owning state it has no
other reason to touch — trace back to the same cause: behaviour was reachable
only by writing an `ImGui::MenuItem` or `ImGui::Shortcut` call, so both paths
had to live where the menu was drawn. Giving behaviour a name (`Command::id`)
that a menu, a keymap, and eventually a command palette can all address
independently removes the reason for that coupling.

## Consequences

### Positive

- A menu item's label can never disagree with what its shortcut does; both
  read the same `core::Chord`.
- A binding can be changed by the user, because it is data (`core::Keymap`)
  rather than a call site.
- `enabled`/`checked` predicates are written and tested once, headlessly,
  against `core::Command`, instead of once per `ImGui::MenuItem` call.

### Negative

- Adding a menu item now means registering a `core::Command` with an id —
  more ceremony than an `ImGui::MenuItem` call with a lambda inline.
- A command's id is what a user's stored keymap override is keyed on.
  Renaming an id silently discards that user's binding for it; nothing
  detects the rename or migrates the override.

### Neutral

- `core::Chord`, `core::Command`, `core::CommandRegistry`, and `core::Keymap`
  live under `Core/src/Core/Command/`, one type per file, per
  [0002](0002-keep-core-free-of-ide-knowledge.md): the registry and keymap
  know nothing of ImGui or of Graphite's panels.

## Alternatives Considered

### Keep shortcuts inline, generate menu labels from them

Have `OnUIRender` build the label string from the same `core::Chord` passed
to `ImGui::Shortcut`, without introducing a command id or a registry. This
would fix the label/binding drift but not the rest: the state changed by a
menu item would still live in `GraphiteLayer` by default, and there would be
no single place to disable a shortcut and its menu item together, or to
rebind a chord without editing source.

## Related

- [0002](0002-keep-core-free-of-ide-knowledge.md) — the boundary this record's
  `Core/Command` types are held to.
