# 0655. Start a branch review on its first viewed mark and keep drafts out of reviews.json

Status: proposed
Date: 2026-10-28

## Context

[0586](0586-treat-a-branch-review-as-a-session-with-a-fixed-base.md) made a review a session that the branch menu starts with "Review against". Looking at a branch's changes and starting a review of it were one act, so every branch the reader only glanced at either had no review view or got a session, a badge and an entry in `reviews.json` it never asked for.

The Git window now shows a branch's changes in a thread view. That view needs a base and settings (whether uncommitted work is included, the list shape, whitespace) even for a branch nobody has reviewed. Those must be held somewhere while the reader looks, without claiming that a review is under way.

## Decision

A branch that is only looked at holds its base and settings in a draft: a session with `Draft` set.

- `OpenDraft` creates a draft for a branch that has neither a draft nor a session. It opens with uncommitted work included. A branch that already has either is left alone.
- A draft becomes a kept session on the first viewed mark (`Mark`), on `Keep`, or on `Start`. `Start` over a draft takes the new base and keeps the draft's settings. `Start` on a branch with neither opens a session with uncommitted work included, as a draft opens, which amends [0581](0581-show-a-branch-s-uncommitted-work-as-a-group-of-its-own-in-its-review.md)'s default. `Start` over a kept session still changes nothing.
- A draft is not written to `reviews.json` and shows no badge. `IsKept` tells a kept session from a draft; `Find` answers for both, so the view reads its settings the same way.
- `Prune` and `Rename` treat a draft like a session, so a draft follows a renamed branch and goes with a deleted one.
- The branch menu offers "Review" for a branch without a kept session, opening its thread against the default base, and a "Review against" submenu with every other local branch. A kept session offers "Open review" and "End review…" as before.

## Rationale

**A review begins when the reader does some reviewing.** A viewed mark is the first thing a review keeps; before it, there is nothing to lose by forgetting the branch.

**One type for both.** A draft is a session with a flag, so the view reads base and settings through one path and a mark needs no conversion step beyond clearing the flag.

**The submenu is always there.** Choosing another base is useful for every branch, not only when HEAD is detached.

## Consequences

### Positive

- Glancing at a branch leaves no badge and no entry in `reviews.json`.
- A branch's thread has a base and settings from the first frame.
- A draft changes the sessions' version like any other change, but the store skips a write whose text equals the text it last wrote or read, so a glance never rewrites `reviews.json`, never creates it, and never takes the one-time copy of a file it could not read.

### Negative

- The store encodes the sessions on every flush that follows a change, to tell a glance from a change.
- Code that asks `Find` whether a branch is under review gets a draft as an answer and must ask `IsKept` instead.
- A draft is lost when the folder is closed; its settings come back as defaults.

### Neutral

- The base of a draft can still be replaced, by `Start`, until a mark keeps it. [0586](0586-treat-a-branch-review-as-a-session-with-a-fixed-base.md)'s fixed base holds from the moment a session is kept.

## Alternatives Considered

- **Start a session whenever a branch's thread is opened.** Simple, but every branch looked at gets a badge and a stored session.
- **Hold the settings of unreviewed branches in the view.** Keeps `reviews.json` clean, but the base and settings would then be read from two places and lost when the view is rebuilt.

## Related

- [0586](0586-treat-a-branch-review-as-a-session-with-a-fixed-base.md): the session this amends.
- [0583](0583-keep-review-marks-in-their-own-file-beside-the-window-state.md): the file drafts stay out of.

Superseded in part by [0669](0669-show-references-in-a-sidebar-beside-the-history.md): the thread view is gone with the threads. The branch menu's "Review" and "Open review" open the review tab, which keeps the draft as a session at once, so a draft lasts only from `OpenDraft` to `Keep`. The draft itself, the first viewed mark that keeps it, and drafts staying out of `reviews.json` are unchanged.
