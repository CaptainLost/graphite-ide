# 0633. Show every wait with the Core loader and say nothing is there only after an answer said so

Status: proposed
Date: 2026-10-25

## Context

[0576](0576-draw-a-loading-indicator-through-a-core-facade-over-imspinner.md) gave the
application one loader, `Core::LoadingState` and `Core::LoadingLine`, which shows after a
fifth of a second. By October 2026 it was used in about twenty places, almost all of them
a view's first load. An audit of every asynchronous request found the other waits drawn
as nothing at all, or as their result's empty case:

- the References panel said "0 hits" and F12 "no definition found" when the server had
  not started or had failed, because a failed answer was handed on as an empty list;
- the Problems panel said "No problems in open documents." while Roslyn was still loading
  projects and no diagnostics had been asked for;
- a commit's diff tab said "This diff is no longer loaded." while the diff was being read;
- the Explorer filter said `No files match "Foo"` while the file list was being re-read;
- a review tab said "Nothing to review." while its uncommitted changes were loading.

A message that says there is nothing is read as an answer. Shown during a wait, it sends
the reader off to look elsewhere for something that was about to appear.

## Decision

- **Draw every wait the reader started or is looking at with the Core loader**:
  `LoadingState` for a whole area, `LoadingLine` for a row, a header or a tooltip. No view
  draws a spinner of its own.
- **Draw an empty result only after an answer that was empty.** "No …", "0 hits", "not
  found" and "Nothing to …" wait for that answer. Until then, draw the loader. After a
  failure, draw the failure and its reason, with Retry where asking again can help.
- **Keep earlier content on screen while it is refreshed**, and add a `LoadingLine` saying
  what is being refreshed. Do not blank a view, and do not dim it, for a refresh.
- **Keep a failed request, or one that could not be sent, apart from an empty answer.** A
  language server that is not running, not ready or answered with an error is named with
  its state ("clangd stopped", "waiting for Roslyn to load Graphite.csproj"). It is never
  drawn as an empty list.
- **Word a server's state in one place**, shared by lenses, References, navigation, hover
  and Problems.

## Rationale

The loader already existed and already solved flicker with its delay. What was missing was
a rule saying when to use it, and that the empty case is an answer, not a default.

## Consequences

### Positive

- A reader can tell "still working", "failed because…" and "there is nothing" apart
  everywhere.
- A refresh no longer makes content jump or flash empty.

### Negative

- Models must tell views more than they did: whether a request is in flight, whether it
  failed and why. Several had only "have a value or not".
- Content kept on screen during a refresh can be acted on while it is out of date. Views
  where that matters, such as the Changes panel after a write, disable their controls
  until the refresh lands.

### Neutral

- First loads that already used the loader are unchanged.

## Alternatives Considered

- **Dim stale content instead of a loader line.** A dimmed diff or file list is hard to
  read, and dimming already means something else in places: stale diagnostics and preview
  tabs.
- **A global busy indicator in the status bar only.** It says something is happening but
  not where, and it does not stop a panel from claiming there is nothing.

## Related

- [0576](0576-draw-a-loading-indicator-through-a-core-facade-over-imspinner.md): the loader.
- [0634](0634-give-every-language-server-request-a-deadline.md): how a wait on a server ends.
- [0635](0635-report-each-git-write-in-the-status-bar-from-the-click-until-the-status-after-it-lands.md):
  waits on git writes.
- [0638](0638-show-where-a-navigation-command-waits-and-how-it-ended-in-a-bubble-at-the-caret.md):
  waits on navigation.
