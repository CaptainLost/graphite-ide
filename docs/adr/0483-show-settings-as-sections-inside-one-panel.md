# 0483. Show settings as sections inside one panel

Status: superseded by 0573
Date: 2026-08-13

## Context

The key binding editor was its own `Core::Panel`, with its own window, its own
visibility toggle, and its own place in the panel manager. Adding a second
screen of settings — editor preferences — the same way would give the
application two panels named "Settings" in spirit, each independently docked,
independently shown or hidden, and independently reachable from wherever
settings are opened from. Three further sub-projects (B, C, D) are expected to
add more such screens: font rendering, colours, keymaps beyond the current
editor. Multiplying panels does not scale with that.

`SettingsPanel` exists instead: one `Core::Panel` that owns a list of
`SettingsSection`, shows exactly one at a time, and lets a side list switch
between them.

## Decision

`SettingsSection` is a small interface — `GetTitle`, `OnImGuiRender`,
`OnEvent`, `OnDeactivated` — that a screen implements to plug into
`SettingsPanel`. A section is not a `Core::Panel`.

`Panel` carries window management: visibility, `WasDrawn`, and its own
`Begin`/`End` pair around an ImGui window. A section renders into a region
`SettingsPanel` has already opened; if a section called `ImGui::Begin` of its
own, or inherited `Panel`'s, the result would be an ImGui window nested inside
another window's body, which ImGui does not support as a layout — the nested
window would float free of the region rather than fill it. So a section gets
none of `Panel`'s machinery and answers none of `Panel`'s questions
(`IsVisible`, `WasDrawn`) — it does not need to, because `SettingsPanel` is
the only thing that decides whether a section is on screen.

Events reach only the active section. `SettingsPanel::OnEvent` forwards
straight to `m_Sections[m_Active]`; a section not currently shown is not
offered anything. `KeyBindingsSection` captures chords while recording a
binding, and a capture in a section that is not on screen must not steal keys
aimed at whichever section actually is — the same reasoning [0480]
(0480-drive-input-from-events-and-leave-the-drawn-gestures-to-the-gui.md)
applies to panels generally, here applied to sections one level down. The
other side of the same requirement is that the active section must not be
starved: `SettingsPanel` forwards unconditionally, with no separate gate that
could suppress delivery while a section is genuinely the one on screen.

`SettingsSection` gives two of its four functions bodies — `OnEvent` and
`OnDeactivated` default to doing nothing — which departs from
[0099](0099-c-121-if-a-base-class-is-used-as-an-interface-make-it-a-pure-abstract-class.md),
which asks that every member function on an interface be pure virtual with no
bodies in the base. 0099 names the cost of following it exactly: "every
implementation must supply every function, so behaviour common to all of them
is duplicated." `EditorSection` holds no transient state and has nothing to
do when deactivated or offered an event it does not capture; forcing it to
write two empty overrides is exactly the duplication 0099 warns is the price
of purity, paid here for two methods on a section that has no use for either.
`GetTitle` and `OnImGuiRender` stay pure, because every section has an answer
for both.

## Rationale

The interface is shaped around what changes between a section and a `Panel`,
not around a wish to reuse `Panel`'s code: a section is drawn content with a
lifecycle, not a window. Giving `OnEvent` and `OnDeactivated` default
no-op bodies rather than requiring every section to override them keeps the
interface's four-method shape stable as sections are added without punishing
the sections — the majority, expected to be `EditorSection`'s kind rather than
`KeyBindingsSection`'s — that need neither.

## Consequences

### Positive

- A section cannot nest an ImGui window inside `SettingsPanel`'s body, because
  it has no `Begin` of its own to call.
- A section that captures input, like `KeyBindingsSection`, cannot swallow
  keys meant for a section that is not itself, because only the active
  section is ever offered an event.
- Adding a section that needs neither `OnEvent` nor `OnDeactivated` costs one
  class with two methods, not four.

### Negative

- There is no `OnActivated`. A section cannot cheaply refresh itself at the
  moment it becomes the one shown — `KeyBindingsSection`'s table happens to
  read live state on every frame regardless, but that is not something the
  interface guarantees or a future section can rely on.
- `SettingsPanel::SelectSection` takes an index into `m_Sections`, not a
  stable id. Inserting a section in the middle of the vector silently changes
  what any remembered index refers to — a command or a test that stores
  "section 1" now points at a different screen.
- Closing the panel does not call `OnDeactivated` on the active section. A
  chord capture in progress in `KeyBindingsSection` survives the window
  closing and resumes untouched when the panel reopens, because nothing told
  the section it was left.
- `SettingsSection` departing from 0099 means a reader who has internalised
  0099 will see `OnEvent`/`OnDeactivated`'s bodies and reasonably ask whether
  it was an oversight; this record is the answer.

### Neutral

- Per [0002](0002-keep-core-free-of-ide-knowledge.md), `SettingsSection` lives
  in `Graphite/src/Settings`, not `Core`: it is application vocabulary, and
  nothing about panels-of-sections is reusable by something that is not this
  IDE.

## Related

- [0002](0002-keep-core-free-of-ide-knowledge.md) — why this interface lives
  in Graphite rather than Core.
- [0099](0099-c-121-if-a-base-class-is-used-as-an-interface-make-it-a-pure-abstract-class.md)
  — the rule this record departs from, and why.
- [0478](0478-route-menu-items-and-shortcuts-through-commands.md) — the
  command that opens `SettingsPanel`, and the pattern a future
  `git.ignored.*`-style command in a new section would follow.
- [0480](0480-drive-input-from-events-and-leave-the-drawn-gestures-to-the-gui.md)
  — the event model `OnEvent` is a small, section-scoped instance of.
