# 0565. Keep a session history of toasts behind a bell in the status bar

Status: proposed
Date: 2026-10-07

## Context

A toast that leaves by itself, such as a warning after six seconds or any toast
when its folder closes, was gone for good, and nothing said it had been there
([0557](0557-report-a-failed-action-in-a-toast-rather-than-in-the-panel-that-issued-it.md)).
[0564](0564-show-a-toast-s-countdown-by-draining-its-border.md) shows when a
toast will leave, but not what left.

## Decision

Record every toast that is shown in a history kept for the session, and open it
from a bell at the right end of the status bar.

- The history lives in `Graphite/src/Notifications`. It lists entries newest
  first, counts a twin on its entry, and keeps at most two hundred.
- Core's `ToastStack` returns each toast's id when it is shown and reports how
  each toast left: closed by the user, expired or cleared, and whether the
  pointer rested on it. Core knows nothing of the history.
- An entry is missed when its toast left on its own, with the pointer never on
  it, and was not a confirmation. The bell shows a dot in the colour of the most
  severe missed entry. Opening the popup marks everything seen.
- The bell opens the popup, and a click outside it, the bell included, closes it.
- Confirmations (`Success`) are hidden behind a filter.
- An entry about a folder keeps that folder and shows it when another folder is
  open.
- Nothing is written to disk.

## Rationale

The application layer already owns the toasts (0557) and is the only place that
knows the open folder. Core is the only place that knows how a toast left.
Reporting departures keeps each fact where it is known.

## Consequences

### Positive

- Nothing a toast said is lost within a session.
- The dot answers "did I miss something?" without opening anything.

### Negative

- The history is gone after a restart.
- The bell takes a little of the status bar's right end.
- A toast that arrives while the popup is open appears beneath it, partly hidden,
  and its entry is already at the top of the list. ImGui puts every new window in
  front of an open popup, so the popup and its entry menu raise themselves every
  frame, and toasts are not raised over an open popup.

### Neutral

- Confirmations are recorded but hidden by default.

## Alternatives Considered

- **The history in Core.** Core would learn folders, filters and a list view, and
  the bell lives in Graphite's status bar anyway.
- **Graphite inferring departures by comparing the stack each frame.** It cannot
  tell a close from an expiry.
- **Persisting the history to disk.** It brings a file format, migrations and a
  size limit for a need a session already meets.

## Related

- [0557](0557-report-a-failed-action-in-a-toast-rather-than-in-the-panel-that-issued-it.md)
  — the toasts and who owns them.
- [0564](0564-show-a-toast-s-countdown-by-draining-its-border.md) — when a toast
  will leave.
- [0530](0530-draw-a-frame-only-when-there-is-reason-to.md) — frames are drawn only when there
  is reason to.
