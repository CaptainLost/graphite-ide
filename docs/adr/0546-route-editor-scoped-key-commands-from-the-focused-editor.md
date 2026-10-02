# 0546. Route editor-scoped key commands from the focused editor

Status: proposed
Date: 2026-10-02

## Context

The layer routes every key event to the shortcut router before any widget sees
it, and every key chord was global. A global Ctrl+C would therefore take the
shortcut away from every ImGui text field (the find bar, the rename box, the
settings fields), which handle it themselves.

Whether an editor holds the keyboard is knowable only while drawing, when the
editor can ask ImGui. The layer's event handler runs outside that moment.

A chord is saved to settings as plain text (`Ctrl+Insert`) and carries no scope,
so a scope stored on the chord would be lost the first time the user rebinds it.

## Decision

The scope of a command's key chords lives on `Command::KeyScope`, `Global` by
default, and `Chord::Scope::FocusedEditor` names the new case. The layer keeps
routing every key event as `Global`, so editor-scoped commands pass through it
untouched. The focused editor routes the strokes it reads while drawing as
`FocusedEditor`. `ShortcutRouter::OnKeyPressed` takes the scope and fires only
the commands whose `KeyScope` matches; a binding whose command is not registered
counts as `Global`.

A stroke that ran a command is not also applied as a keystroke by the editor.

`Command::Repeats`, true by default, says whether holding the key runs the
command again on every repeat. A command that sets it false is skipped on
repeated presses.

## Rationale

The command is the one thing that survives rebinding and knows what it does, so
it owns the scope. Routing from the editor, rather than guessing focus in the
layer, is the only way to leave text fields their own clipboard shortcuts.

## Consequences

### Positive

- Rebinding a command keeps its scope, however the chord was typed.
- Ctrl+C, Ctrl+V and the like can be real, rebindable commands without breaking
  any text field.

### Negative

- An editor-scoped command does nothing outside an editor, so it cannot be bound
  to a menu accelerator that must work everywhere.
- Every editor-like widget that wants these commands has to route its strokes
  itself.

### Neutral

- Conflict detection still reports two commands on the same chord across scopes,
  since the chord itself carries no scope.
- Wheel chords keep their own scope on the chord; only key chords moved.
- Strokes the editor replays as `FocusedEditor` do not touch the router's
  modifier-tap state: the layer already counted them as `Global`, and counting a
  replayed Ctrl press again would revive a tap that Ctrl+S had abandoned.

## Alternatives Considered

- **A scope on the chord.** Matches how wheel chords work, but the saved text has
  no place for it, so a user's rebinding would silently turn an editor command
  global.
- **Deciding focus in the layer.** The layer can read `WantTextInput`, but it
  cannot tell an editor from the find bar, and the answer is stale outside the
  draw pass.

## Related

- [0478](0478-route-menu-items-and-shortcuts-through-commands.md) — menu items
  and shortcuts already run through commands; this record adds a scope to them.