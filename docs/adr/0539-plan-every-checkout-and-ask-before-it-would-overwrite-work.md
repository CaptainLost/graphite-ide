# 0539. Plan every checkout and ask before it would overwrite work

Status: proposed
Date: 2026-10-01

## Context

The Git panel can now switch branches, check out a remote branch or a commit,
and so rewrite files the user may be editing. A checkout can destroy work in two
places. Git sees the first: a file with uncommitted changes that the target
differs in. Git cannot see the second: text in an open editor tab that has not
been saved, in a file the checkout is about to rewrite.

The editor reloads a clean document when it is told a write is coming
(`DocumentWindows::ExpectExternalWrite`), and otherwise asks the "file changed
on disk" question. A checkout touches many files at once, so the editor has to
be told which ones.

## Decision

Plan every checkout before running it, with no exception: a new branch created
with "Checkout after creating" is created first and then checked out through the
same plan, which queues behind the create. `Repository::PlanCheckout` is a dry
run that changes nothing on disk, in the index or in HEAD, and answers which
paths the checkout would overwrite and which it would only update. Unresolved
conflicts in the index are listed as overwritten: libgit2 refuses any checkout
over them, a dry run included, without naming a file.

`GitView`, not the model, drives the flow: it asks the model for the plan, and
when the plan answers it reads the unsaved documents under the repository from
the editor - then, not at the click, so a document edited while the plan was
being made is part of the decision. `NeedsCheckoutDialog` decides from the plan
and those paths:

- If nothing would be overwritten and no unsaved document is among the files the
  checkout rewrites, the checkout runs at once. Changes in files the two sides
  do not differ in travel with the user to the new branch.
- Otherwise one dialog asks, with **Discard**, **Stash** and **Cancel**
  ([0540](0540-discard-or-stash-every-change-on-the-way-to-a-checkout-untracked-files-only-on-request.md)).
  Cancel changes nothing.

`Repository::Checkout` is a safe checkout: when the target would overwrite
changes it is refused and nothing changes. A refusal after a plan means the tree
changed in between, and is reported as an error, never as an overwrite. Over
unresolved index conflicts it is refused in words that say so: resolve or
discard the conflicts first.

`Checkout` returns `CheckoutOutcome` rather than `Result`, a deliberate
exception to [0004](0004-return-result-t-instead-of-throwing-across-module-boundaries.md):
a checkout that fails partway has still changed files, and the outcome must name
them alongside the error. A failure is reported as "Could not check out
*target*: ...", and one that stops after Discard or Stash ran says where the
changes went.

An I/O failure partway, such as a file held open by another process, can leave
the tree partly switched. The outcome then names every path that may have
changed, and the editor is told all of them, so no open document keeps stale
text.

The editor reloads a changed file without asking only when its document has no
unsaved edits at that moment, or the user chose in the dialog to revert or save
that document. A document edited after the question - or on a checkout that
asked nothing - gets the editor's own "file changed on disk" question instead.

Paths from git and from the editor are matched ignoring ASCII case and
separator style, because the two spell the same file differently on Windows. A
file inside an untracked directory counts as a blocking untracked file, and an
unsaved untracked file is listed once.

## Rationale

The plan is what makes the question askable: the user is told what is at stake
before anything is touched, and the common case, a checkout that overwrites
nothing, costs no dialog. Keeping the checkout itself safe means a change made
by another program between the plan and the run cannot turn into an overwrite.
The editor's unsaved text is invisible to git, so the decision takes it as an
input rather than trusting git's view alone.

## Consequences

### Positive

- A checkout never overwrites saved or unsaved work unless the user chose to
  discard it.
- A checkout that overwrites nothing runs without a question.
- The decision and the dialog's contents are pure functions, tested without a
  frame.

### Negative

- Every checkout costs a background dry run before it starts.
- A change made between the plan and the run turns into a refusal reported as an
  error, which the user must retry, rather than into a second question.
- A checkout that fails partway cannot be rolled back; the user is left with a
  partly switched tree and the editor is told every path that may have changed.

### Neutral

- `GitView` drives the checkout: plan, decide, optional revert or save of the
  unsaved documents, then the request. The model runs the request's discard or
  stash and the checkout as one job, and reports the changed paths.

## Alternatives Considered

- **Run the checkout and report a refusal.** Needs no plan, but the user learns
  what was in the way only after the fact, git cannot refuse over unsaved editor
  text it cannot see, and no way forward is offered in the same step.
- **Always ask before a checkout.** Simplest to build, but asks even when
  nothing is at risk, which makes the dialog routine and so easy to click past.
- **Force the checkout over the changes.** Makes the dialog unnecessary and
  loses work silently.

## Related

- [0540](0540-discard-or-stash-every-change-on-the-way-to-a-checkout-untracked-files-only-on-request.md)
  — what the dialog's Discard and Stash take.
- [0541](0541-put-branch-and-stash-actions-in-the-git-panel-s-right-click-menus.md)
  — where a checkout is started from.
- [0475](0475-ask-every-modal-question-through-modalquestion.md) — the modal the
  question is asked through.
- [0004](0004-return-result-t-instead-of-throwing-across-module-boundaries.md) —
  how the plan is returned.
