# 0476. Load a tab whose file appears, rather than only clearing its missing mark

Status: proposed
Date: 2026-08-11

## Context

A tab can exist without ever having held content. `DocumentSet::Open` on a path
that is not there adds an item carrying `OpenFailure::Missing` and no document —
a restored session whose file was deleted meanwhile, or a file removed between
the tree listing it and the click.

`SyncWithFile` noticed such a file coming back and called `MarkPresent`, which
clears the missing flag and nothing else. The result was a tab that had stopped
saying the file was gone, held no content, and never would: `FailureMessage`
returns the empty string for `OpenFailure::None`, so the editor drew a centred,
dimmed nothing, and every later sweep read an item that was neither missing nor
holding a document and reported `Unchanged`. The state was terminal until the
user closed the tab.

The outcome that named this, `CameBack`, was returned but never distinguished
from `Unchanged` by any caller — the enumerator recorded a bug rather than a
decision. Acting on it means loading the file, and loading can end four ways the
application already knows: text, binary, unreadable, or large enough that the
user has to be asked first. Asking needs a user, which is why `SyncWithFile`
cannot simply load and be done ([0002](0002-keep-core-free-of-ide-knowledge.md)).

## Decision

A file appearing under an item that carries no document is reported as
`FileSyncOutcome::NeedsLoad`, and the caller loads it.

- Loading lives in `core::text::LoadFromFile(OpenItem&, bool confirmed)`,
  returning `ItemLoad::{Loaded, NeedsConfirmation, Failed}`. It takes an item
  rather than a set and an index because it needs nothing else — the same shape
  `Editing.h` uses.
- `DocumentSet::Add` calls it too, so one function decides what may be opened
  whether an item is being created or filled in later. `Add` keeps what is
  genuinely about the set: the preview slot, the push, the activation.
- `LoadFromFile` clears whatever the item last failed with once it has actually
  looked at the file. `NeedsConfirmation` is the exception: it decides nothing
  and so changes nothing but `size`, the number the question quotes.
- The outcome is named for the work left to do, like `SafeToReload` and
  `NeedsDecision` beside it, not for the event that produced it.
- Only an item that was `Missing` reaches it. `Binary`, `TooLarge` and
  `ReadFailed` also carry no document, but they mean "not opening this", not
  "waiting for the file".
- Answering the size question routes through `DocumentWindows::LoadConfirmed`,
  which fills an already-open empty tab and opens a new one otherwise.

## Rationale

The alternative to acting is not neutrality: a cleared mark on an empty tab is a
worse state than the mark, because it removes the explanation without supplying
the content. Once the application must act, the only question is who loads, and
the answer follows [0002](0002-keep-core-free-of-ide-knowledge.md) — Core
decides and hands back, Graphite asks the user and calls back in. That is the
same division `OpenOutcome::NeedsConfirmation` already draws at open time, so
the reappearing large file gets the question it would have got had the user
picked it by hand.

Extracting `LoadFromFile` rather than writing a second loader is
[0174](0174-es-3-dont-repeat-yourself-avoid-redundant-code.md): two answers to
"what may be opened, and how" would drift, and the rule about the size ceiling
is exactly the kind that gets updated in one place and not the other.

## Consequences

### Positive

- A tab opened on a file that did not exist becomes the file the moment it
  appears, including when it appears too large to open without asking.
- One loader. A change to what counts as binary, or to the size ceiling, reaches
  both the open path and the appear path or neither.
- `Add` is thirty lines shorter and now reads as what it is about — the set —
  rather than as file classification with bookkeeping around it
  ([0439](0439-f-2-a-function-should-perform-a-single-logical-operation.md)).
- The four ways loading can end are now testable headless, and are tested.

### Negative

- `LoadFromFile` mutates an item in place and can leave it in any of five states.
  That is a wider contract than `Add`'s, which always started from a blank item,
  and it is documented in prose rather than enforced by a type.
- A file that reappears while the user is looking at the tab loads under them
  without being asked. This is the same silence as `SafeToReload`, which reloads
  a clean document without asking, so it is consistent — but it is a load nobody
  requested, and a tab that was blank a frame ago now has content.
- One more question can now be raised by a watcher batch rather than by a click,
  so a git checkout restoring a large file puts a modal up on its own. The
  queueing in [0475](0475-ask-every-modal-question-through-modalquestion.md) is
  what keeps that to one question at a time.

### Neutral

- `MarkPresent` is unchanged and still clears only the flag. What changed is that
  something now follows it.
- An item carrying `Binary` or `TooLarge` whose file has since become ordinary
  text stays as it is. Those states are decisions about the file, not about
  waiting for it; revisiting them is a separate question this record does not
  answer.

## Alternatives Considered

- **Fold `CameBack` into `Unchanged` and stop clearing the mark when there is no
  document.** No new API, and the enumerator disappears. Rejected because the tab
  would go on saying "This file no longer exists on disk" about a file sitting
  right there, which trades a blank tab for a false one — and the tab still never
  loads.
- **Load inside `SyncWithFile`.** Fewer moving parts, and the caller needs to
  know nothing. Rejected because a file that came back large has to be asked
  about, and Core has nobody to ask; `SyncWithFile` would have to either refuse
  large files silently or grow a callback, which is the boundary in
  [0002](0002-keep-core-free-of-ide-knowledge.md) leaking through an
  argument.
- **Give the empty tab a "Reload" button and wait for a click.** Nothing happens
  behind the user's back. Rejected because the tab has no content to weigh
  against the file — there is nothing to lose and nothing to choose between, and
  every other outcome with nothing at stake (`SafeToReload`) already acts without
  asking. A button would be a question with one sensible answer.

## Related

- [0002](0002-keep-core-free-of-ide-knowledge.md) — why the decision is
  Core's and the asking is Graphite's.
- [0174](0174-es-3-dont-repeat-yourself-avoid-redundant-code.md) — why `Add` and
  the appear path share one loader.
- [0475](0475-ask-every-modal-question-through-modalquestion.md) — the queue that
  keeps a batch of reappearing large files to one question at a time.
- [0439](0439-f-2-a-function-should-perform-a-single-logical-operation.md) — what
  `Add` was doing before the extraction.
