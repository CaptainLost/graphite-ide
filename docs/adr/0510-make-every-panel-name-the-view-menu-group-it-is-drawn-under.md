# 0510. Make every panel name the view menu group it is drawn under

Status: proposed
Date: 2026-09-07

## Context

The view menu listed its panels through a single command group, `panels`, which
`PanelManager` registered every panel into. The order of the rows was therefore
the order of the `Add` calls in `GraphiteLayer::OnAttach`, which nobody chose
for the menu's sake, and nine toggles arrived as one undivided list. Beside
them sat editor settings, a zoom trio and a submenu called `Git` — a word that
in the same menu already named a panel.

Dividing the list needs somewhere for the division to live. Naming the panel
commands one by one in the menu tree would put it there, but a panel added
later would then be absent from the menu with nothing to say so: the tree
compiles, the tests pass, and the panel is simply unreachable.

## Decision

A panel names its menu group when it is constructed —
`Panel(id, title, menuGroup, visible)` — and `PanelManager` registers its
toggle into the command group `"panels." + menuGroup`. The parameter has no
default, so a new panel cannot omit it.

The menu tree names those groups rather than the panels. A row that carries a
state to choose between — not a toggle, not an action — is what earns a
submenu; everything else keeps the level it is read on, so a printed shortcut
is read rather than hunted for.

## Rationale

Where a panel belongs is knowledge the panel has and the menu does not; asking
for it at the one place a panel is defined keeps the two in step without either
holding a list of the other. Leaving the parameter undefaulted turns the one
failure this arrangement can have — a panel outside every group the menu draws
— into an error the compiler reports rather than a row nobody notices is gone.

## Consequences

### Positive

Adding a panel places it in the menu with no edit to the menu tree. The section
a panel appears in stops depending on the order `OnAttach` happens to add them.

### Negative

Core now carries the idea that a panel belongs to a named group of a menu it
knows nothing else about. The group is a bare string, so a panel naming one the
menu does not draw still disappears — the compiler enforces that a group is
named, not that it is one of the four that exist.

### Neutral

Order within a group is still registration order. Nothing so far has wanted it
otherwise.

## Related

- [0002](0002-keep-core-free-of-ide-knowledge.md) — the group is a string Core
  never interprets, which is what keeps this out of Core's knowledge of the IDE.
