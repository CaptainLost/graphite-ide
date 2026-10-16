# 0593. Send editor commands to the editor view that has the keyboard

Status: proposed
Date: 2026-10-16

## Context

Editor commands acted on `DocumentSet`'s active index ([0546](0546-route-editor-scoped-key-commands-from-the-focused-editor.md), [0547](0547-make-the-active-document-follow-keyboard-focus-into-its-editor.md)). A view that is not a `DocumentSet` entry, such as the results tab, could not receive them.

## Decision

- Editor commands act on an `EditorCommandTarget`, the view `ActiveEditorTarget` names. These are cut, copy, paste, select all, toggle comment, undo, redo, save, close, find, go to definition and find references.
- A view sets itself active when its window takes keyboard focus. The file tabs do it in the same place 0547 activates the document.
- Commands keep their ids, keys and scopes: `FocusedEditor` for editing, save and close; `Global` for find, F12 and Shift+F12.
- Indent and outdent stay keystrokes the widget handles; they are not commands.
- Language commands ask the target for the file at its caret.

## Rationale

- One registration serves many kinds of view.
- The file-tab behaviour is unchanged: the target forwards to the code that ran before.

## Consequences

### Positive

- A new view gets every editor command by implementing one interface.

### Negative

- A view must remember to set itself active, and to stop being the target when it closes.

### Neutral

- Amends 0546 and 0547. The active view follows keyboard focus; the active document still does too.

## Related

- [0546](0546-route-editor-scoped-key-commands-from-the-focused-editor.md)
- [0547](0547-make-the-active-document-follow-keyboard-focus-into-its-editor.md)
