# 0547. Make the active document follow keyboard focus into its editor

Status: proposed
Date: 2026-10-02

## Context

Every command that acts on "the document" acts on the active one in the
`DocumentSet`. That includes F12, Alt+O (switch header/source), and the
clipboard and comment commands of the editor. New tabs also dock into the node
of the document that was activated last.

A document became active only when ImGui reported its window as focused, asked
without flags. A click on the text never focuses that window. It focuses the
editor surface, which is a child window inside it. So in a split, the active
document changed only when a tab was clicked. Take two halves, A and B. Click
B's tab, then click into A's text. B stays active, and Ctrl+C typed in A copies
B's selection. A right click moves no focus at all, so a menu opened over A
would act on B as well.

## Decision

A document window counts as active whenever it or any window inside it holds the
keyboard (`ImGuiFocusedFlags_ChildWindows`). That covers the editor surface, the
find bar inside it, and any popup opened from the window.

A right press on the editor's text focuses the surface before the context menu
opens. The document under the menu becomes active the same way a left click
makes it.

## Rationale

The fix stays at the single place that decides activation, so every command that
reads the active document follows without changes of its own. The user's notion
of "this document" is where they typed or clicked last, and keyboard focus
already tracks exactly that.

## Consequences

### Positive

- F12, Alt+O, the editor's clipboard and comment commands, and new-tab placement
  all follow the split the user clicked into.
- The editor's context menu acts on the document it was opened over.

### Negative

- Activation is read before the editor draws in the same frame. On the frame a
  right press opens the menu, the menu's enabled state is still computed from
  the previously active document. From the next frame on it is correct.
- Opening a tab's own right-click menu now also activates that document, since
  the popup counts as inside its window.

### Neutral

- Focus in a panel or a terminal still leaves the active document where it was;
  only windows inside a document window count.

## Alternatives Considered

- **Bind each command to the editor that drew it.** The commands would act on
  the editor currently drawing, not on the active document. This would leave
  activation as it was, but every command would need a second notion of "the
  document" and F12 would still act on the wrong split.
- **Activate from inside the editor widget.** The widget does not own the
  `DocumentSet`; it would have to report focus back and have the caller act on
  it a frame late, which is what the flag already gives for free.

## Related

- [0546](0546-route-editor-scoped-key-commands-from-the-focused-editor.md), whose
  editor-scoped commands depend on this.
