# 0659. Let each shortcut say whether it fires while the user types

Status: proposed
Date: 2026-11-02

## Context

A user bound a panel to the plain key F. The layer routes every key event to
the shortcut router as `Global` before any widget sees it, and a `Global`
command fires wherever the keyboard is held. So every "f" typed into the editor
also toggled the panel. In a focused terminal the same chord is taken from the
shell, because the terminal leaves to the router every chord bound to a
`Global` command (0546).

Text fields were the opposite extreme. `ImGuiRenderer` consumed every keyboard
event while ImGui wanted text input (0480), so no shortcut fired in the find
bar, the rename box or a settings filter, not even Ctrl+Shift+S (Save All).

The user asked for flags on a binding, one of them saying whether it may fire
while typing in the editor. Of the mock-ups shown (2026-11-02) they chose:
flags in a When column, with each chord of a command flagged on its own; a
default under which a chord that types a character waits; and flags for the
terminal, for text fields, and for repeating while held.

## Decision

**A chord carries flags.** `Core::ChordFlags` holds four optional booleans:
fires in the editor, fires in the terminal, fires in text fields, and repeats
while held. Unset means the default. `Chord::IsSameGesture` compares keys,
trigger and scope only, and everything that asks who owns a stroke or whether
two slots hold one shortcut (`Keymap::BoundTo`, `FindConflicts`, the key
bindings page) asks it, not `==`.

**The defaults** (`Core/Command/TypingRules`):

- A chord that types a character waits in the editor, the terminal and text
  fields. That means a printable key or the space, alone or with Shift.
- In text fields a chord also waits when the field edits with it: the caret
  keys, Enter, Esc, Tab, and Ctrl(+Shift) with A, C, V, X, Y or Z.
- Every other chord fires everywhere.
- A chord repeats as its command's `RepeatsWhileHeld` says.

A flag the user sets back to the default is cleared rather than stored.

**Where a stroke lands** is a `TypingTarget`: Nothing, Editor, Terminal or
TextField.

- The focused editor and the focused terminal claim the keyboard while they
  draw (`Core::TypingClaim`). The layer releases the claim as each frame
  begins.
- A stroke arriving before the next frame is routed against that claim.
- When ImGui wants text input, the layer routes the stroke as `TextField`, and
  only to the router; the panels and the editor queue never see it.
- `ImGuiRenderer` no longer consumes a stroke typed into a field. It consumes
  only while a popup is open, as before.

**The router asks the matched chord.** `ShortcutRouter::OnKeyPressed` and
`OnKeyReleased` take the typing target.

- A `Global` command fires only if its matched chord fires there.
- Repeats follow the chord's repeat flag over the command's.
- An editor-scoped command ignores the typing flags, since the editor holding
  the keyboard is the only place it ever fires.

The terminal asks `Keymap::BoundTo` with `TypingTarget::Terminal`, the same
question the router asks, so a stroke goes to exactly one of the shell and the
command.

**The text** of a binding writes a chord's flags in braces after it, only when
any is set: `F {-editor +terminal}|Ctrl+Shift+E`. The words are `editor`,
`terminal`, `fields` and `repeat`, each prefixed `+` (fires, repeats) or `-`.
An unknown word or an unclosed brace refuses the whole binding, as an unknown
key name already does. A binding saved before flags existed reads unchanged.

**The key bindings page** has a When column between Alternates and Default.

- Its combo summarises the command's chords: "Everywhere", "Not while typing",
  "Not in the terminal", or, when the chords differ, "F: not while typing".
  The summary is drawn in the warning colour when the chords wait somewhere.
- Its menu has one tick per place for a command with one chord. For several
  chords it shows a grid of chord by place, with a Repeat column.
- An editor-scoped command shows "Editor only" and offers only repeating.
- A command with no key or tap chord has no menu.
- The row's Default restores the flags with the chords.

## Rationale

The flags belong on the chord: one command can want its letter to wait while
its Ctrl chord fires everywhere, and the chord's text is the one thing a
rebinding rewrites. Defaulting letters to wait fixes the reported case without
touching Settings. A shortcut on a printable key can only have been meant for
moments when nothing is typed, because typed, the key already means its
character. Letting text-field strokes reach the router lets the user opt
commands in there. The defaults still keep a field's own editing keys, and
every character, with the field.

## Consequences

### Positive

- A command on a plain letter no longer fires while typing in the editor, the
  terminal or a text field. It fires when focus is on a panel or nothing.
- A global chord can be given back to the shell (Ctrl+R) without unbinding it.
- Global shortcuts with Ctrl or Alt and the function keys now fire from text
  fields: Ctrl+Shift+S saves everything from the find bar. Editor-scoped ones
  (Ctrl+S, Ctrl+Z and the rest of 0546's list) still fire only in the editor.

### Negative

- A behaviour change: shortcuts that never fired in a text field now do by
  default, unless they edit text there. Alt+O in a rename box switches to the
  header or source file.
- The claim is one frame old, so a stroke arriving in the same instant as the
  click that moves focus is routed against where the keyboard was.
- A letter the user explicitly lets fire in the editor still types its
  character as well, because the character arrives as its own event. This was
  already the case for every letter binding before this record.
- AltGr is not treated as typing. Windows reports it as Ctrl+Alt, so a binding
  on Ctrl+Alt+S still fires while typing "ś". The flag can switch it off.
- Strokes typed into a text field inside a popup still reach no shortcut.

### Neutral

- Wheel chords have no typing flags. The pointer aims them, not the keyboard.
- `GuiConsumesKeyboard` now takes only whether a popup is open.

## Alternatives Considered

- **Flags on the command (the When column for the whole command).** Simpler,
  and the user's first proposal. Rejected in the mock-up review: unticking the
  editor for F would switch it off for Ctrl+Shift+E too.
- **Flags behind a chevron on each chord chip.** No new column, but the
  control is small and easy to miss, and a click on the chip already records a
  new chord.
- **A fixed rule with no flags.** Letters never fire while typing, everything
  else always does. It fixes the report, but gives no way to free Ctrl+R for
  the shell or to let a command fire from text fields.
- **Asking ImGui for the focused window in the layer.** The layer cannot tell
  an editor from any other window. The widgets know, which is why they claim.

## Related

- [0480](0480-drive-input-from-events-and-leave-the-drawn-gestures-to-the-gui.md):
  the GUI no longer takes a stroke because a field wants typing; the layer hands
  it to the shortcuts that fire in text fields.
- [0546](0546-route-editor-scoped-key-commands-from-the-focused-editor.md): the
  terminal now leaves to the router only the chords that fire in the terminal.
