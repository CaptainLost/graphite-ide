# 0515. Ask before every discard in the changes panel

Status: proposed
Date: 2026-09-22

## Context

`BeginDiscard` opened a confirmation only when the discard would overwrite an
open document with unsaved edits. Every other discard ran on the click: a
tracked file's unstaged edits, an untracked file, and — once untracked
directories became rows — an untracked directory with everything under it.

The test it applied was the wrong one. It asked whether the editor was about to
lose something, which is a real loss but the smaller one, and said nothing
about the loss that is always there. Git can hand back none of what a discard
throws away. A tracked file's unstaged edits were never committed and were
never in the index, so no ref and no reflog names them. An untracked file and
an untracked directory git has never seen at all — `DeleteUntracked` calls
`std::filesystem::remove_all` on a directory, which takes ignored files under
it too, and the file system is the only thing that ever held any of it.

The explorer panel already confirms every delete. The changes panel was the one
place in the application where a single click destroyed work silently, and the
discard control sits a few pixels from the stage control, which is reversible.

## Decision

Every discard in the changes panel asks first, on every row and on the folder
row, whether or not any document is dirty.

The question names the loss rather than asking in general terms, because what
is about to go differs by case and only the sentence says which case the reader
is looking at:

- an untracked directory — "Delete `<name>`/ and the N files in it?" when the
  expansion's listing is already held, and "…and everything in it?" when it is
  not;
- an untracked file — "Delete `<name>`?";
- a tracked file — "Discard the **unstaged** changes to `<name>`?".

That last wording is exact and deliberate. `DiscardRequest::Restore` reaches
`Repository::DiscardWorkTree`, which calls `git_checkout_index` — the restore
comes from the **index**, not from HEAD. A partially staged file therefore
keeps its staged content and loses only the edits made on top of it. Saying
"discard changes since the last commit" would name a loss that is not about to
happen.

The dirty-buffer warning survives as a second line under a separator, because
it is a different loss: what git never saw, and so the one part no refresh of
the working tree could put back either.

The question is asked through `ModalQuestion`, as
[0475](0475-ask-every-modal-question-through-modalquestion.md) requires. The
raw `ImGui::BeginPopupModal` that `RenderDiscardConfirm` used is gone with it.
Two withdrawals go with that:

- when the panel stops being drawn (`!WasDrawn()`), and
- when the status goes away, which is what closing the workspace looks like
  here.

`Core::PanelManager` calls `OnUpdate` on every panel every frame but skips
`OnImGuiRender` for one that is hidden, collapsed or behind another dock tab.
A question left open when drawing stops would hold the single application-wide
`ModalGate` forever, and no modal question anywhere in the application could be
asked again.

A repeat is suppressed by subject: `Asked` is consulted before `Ask`. A
question waiting its turn behind another modal leaves the row clickable, and a
second click there would queue a duplicate that asks again about a file the
first answer already discarded.

## Rationale

The rule is not new; it is the rule the explorer panel already followed, now
applied where it was missing. One rule for destroying work is worth more than a
per-case judgement about which destruction deserves a dialog, because the
judgement is what was wrong: the old condition happened to fire for the case
where the loss was recoverable from an open buffer and stayed silent for the
cases where nothing could recover it.

Naming the subject and the exact loss is what makes the question worth asking.
A dialog that says only "Are you sure?" trains the reader to click through it,
and the reader who has clicked through it once has learned to click through the
one about the directory too.

## Consequences

### Positive

- No click in this panel destroys unrecoverable work without a sentence saying
  what it is about to destroy.
- The confirmation's wording matches what the code does, down to restoring from
  the index rather than from HEAD, so a partially staged file's discard is not
  overstated.
- The panel no longer hand-rolls a modal. It gains the dead-popup backstop, the
  queue and the gate discipline of ADR 0475 without writing any of them.

### Negative

- A discard is now two clicks where it was one. For a reader deliberately
  discarding a run of files this is friction on every row, and there is no "do
  not ask again" — which is the point, but it is a cost paid by the reader who
  did mean it.
- Withdrawing the question when the panel stops being drawn drops it, queue and
  all. A discard clicked while another modal held the gate disappears without a
  word if the panel is hidden before its turn comes. That is the lesser harm —
  nothing is discarded and the row is still there to click again — but it is a
  question silently lost, and it is the only way this panel can lose one.
- The folder row's file count is shown only when the expansion's listing is
  already held, so the same directory asks with a number or without one
  depending on whether it has been opened. Asking git for a count here is no
  answer: it would arrive after the question did, or never.

### Neutral

- The Discard button carries the same `IsWriteInFlight` guard every other write
  control in this panel takes; the question does not change when a write may be
  issued.
- Cancelling is an answer: it releases the gate like any other, so a question
  behind it opens in the same frame.

## Alternatives Considered

- **Keep confirming only when unsaved buffers are at risk.** Fewer dialogs, and
  it covers the case where the user has visibly typed something. Rejected
  because it protects the recoverable loss and not the unrecoverable one — the
  buffer is still open and still holds its text either way, while the untracked
  directory the same button deletes is simply gone.
- **Confirm only for the losses git cannot undo, and let a tracked file's
  discard run on the click.** It would have kept the common case at one click.
  Rejected because git cannot undo that one either: unstaged edits were never
  written anywhere git can reach. There is no case in this panel on the safe
  side of the line, so the line would sit nowhere.
- **Make discard undoable instead of asking — stash what is about to go.** No
  dialog at all, and a way back afterwards. Rejected as a much larger change
  with its own surface: a stash is a repository write, so it would have to
  enter the write queue, it changes what the panel then shows, and it has no
  answer for the untracked directory that `remove_all` takes ignored files out
  of. Worth reconsidering only if a general undo for repository writes ever
  exists.

## Related

- [0475](0475-ask-every-modal-question-through-modalquestion.md) — the
  mechanism this question is asked through, and why the panel does not
  hand-roll one.
- [0514](0514-expand-an-untracked-directory-by-asking-git-about-that-directory-alone.md)
  — the folder row whose discard is the largest of these losses, and the
  listing the file count comes from.
