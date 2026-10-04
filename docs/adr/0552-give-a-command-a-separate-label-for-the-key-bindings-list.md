# 0552. Give a command a separate label for the key bindings list

Status: proposed
Date: 2026-10-04

## Context

A command's `Label` is written for the menu it appears in, and a menu supplies
context the label leans on: under View > Ignored Files, "Show", "Dim" and
"Hide" are clear, and a panel's entry in the View menu is just its title,
"Problems". The key bindings list in Settings shows every command in one flat
table with no menu around it, where "Show", "Dim" and "Problems" say nothing
about what the command does.

## Decision

`Core::Command` carries an optional `ListLabel`. A list of commands with no
menu around it - the key bindings list today - shows `ListLabel` when it is
set and `Label` otherwise. Menus keep showing `Label`.

Commands registered for panels read "Show <Title> Panel" in the list, so the
Problems panel's command is listed as "Show Problems Panel" while the View menu
still says "Problems".

## Rationale

The two places answer different questions. A menu is browsed by where things
are, and repeating the menu's own words in every item makes it longer to read;
a flat list is searched by what a command does, and needs the whole sentence.
Keeping both on the command means the registration states both once, next to
each other, instead of the list guessing a longer label from the group.

## Consequences

### Positive

- Every row of the key bindings list says what the command does without the
  menu it came from.
- Menus stay short.

### Negative

- A command whose menu label changes may need its list label changed too, and
  nothing checks that they still agree.

### Neutral

- Most commands leave `ListLabel` empty: their menu label already reads well
  on its own.

## Alternatives Considered

- **Prefix the list entry with the command's group or menu path.** Lost because
  group ids are internal names ("panels.tools") and a menu path ("View >
  Ignored Files > Show") reads worse than a sentence.
- **Lengthen the menu labels.** Lost because every menu would repeat its own
  heading in each item.

## Related

- [0478](0478-route-menu-items-and-shortcuts-through-commands.md) - menu items
  and shortcuts run through commands.
