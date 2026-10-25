# 0638. Show where a navigation command waits and how it ended in a bubble at the caret

Status: proposed
Date: 2026-10-25

## Context

F12, Ctrl+click and Alt+O ask a language server and act on the answer. Between the ask and
the answer nothing was drawn, sometimes for up to 30 s while a server started. When an
answer was empty or failed, a centred message showed in the status bar for 4 s. That is far
from where the reader was looking, and the same text served for "nothing found" and "could
not ask". When the caret moved before the answer, the answer was dropped without a word.

## Decision

- **Show the wait in a bubble.** A small bubble in the hover tooltip's style is anchored at
  the asked word, or at the caret when the command came from the keyboard. It appears after
  the loader's fifth of a second, with the loader and what is awaited:
  - "Finding definition…"
  - "Looking for the header/source counterpart…"
  - or the server's waiting text.
- **End it where it started.**
  - A jump or a choice popup closes the bubble.
  - "No definition found", a server state ("clangd stopped") or "clangd did not answer"
    replaces its content for a couple of seconds, instead of the status-bar message.
- **Cancel with the bubble.** Moving the caret, typing, Escape or switching tabs cancels the
  question and closes the bubble. A second F12 replaces the first. A bubble whose word
  scrolls out of view is hidden.

## Rationale

The reader looks at the word they clicked. A message there answers "did it hear me?" and
"why did nothing happen?" without a glance away. It also makes dropping a late answer
visible: the bubble goes when the reader moves on.

## Consequences

### Positive

- Every navigation command shows that it is working and how it ended.

### Negative

- One more floating element in the editor, with its own anchoring and lifetime.

### Neutral

- The choice popup for several definitions is unchanged.

## Alternatives Considered

- **A wait cursor.** The GLFW backend has no wait or progress cursor, and a cursor says
  nothing to F12 pressed on the keyboard.
- **The status bar.** That is where the messages were. It is out of the reader's line of
  sight.

## Related

- [0633](0633-show-every-wait-with-the-core-loader-and-say-nothing-is-there-only-after-an-answer-said-so.md),
  [0634](0634-give-every-language-server-request-a-deadline.md)
