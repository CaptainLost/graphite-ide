# 0655. Draw Changes as one view hosted by the side panel and the Git window, sharing one commit draft

Status: proposed
Date: 2026-10-28

## Context

The working tree's changes are drawn by `ChangesPanel`, a `Core::Panel` beside the Explorer. It holds everything: the Conflicted, Staged and Changes sections, the rows with their buttons and menus, folders listed on demand, the commit box with its message and Amend, the discard and stash questions ([0475](0475-ask-every-modal-question-through-modalquestion.md), [0515](0515-ask-before-every-discard-in-the-changes-panel.md)), and the work done in `OnUpdate` after a write lands. Clicking a row opens a diff tab.

The Git window is getting a Changes page of its own, with the selected file's diff beside the list. The side panel stays. Both must show the same thing in the same way, a message started in one must show in the other, and each must keep its own selection and open folders.

Two things in the panel must happen exactly once per write, whichever host made it. `GitModel::TakeCommitted` and `TakeDiscarded` each answer once, and their answers must be acted on within a frame of the write: a commit clears the draft, and a discard tells the editor which files are about to change on disk. A discarded folder is gone by then, so its files are remembered when the discard is confirmed and read when it lands.

## Decision

Draw Changes through one class, `ChangesView`, and give each host an instance of its own.

- `ChangesView` draws the lists, the commit box and the questions into the current window. It keeps the selection, the open folders, the collapsed sections and both questions.
- A host decides what picking a file does, through `OnFileChosen`. The side panel opens a diff tab, following the one working-tree tab rule of [0538](0538-refresh-the-working-tree-diff-when-the-repository-changes-not-when-it-is-asked-for.md). The Git window will show the diff beside the list.
- A host that does not draw its view on a frame calls `WithdrawQuestions`, so a question nobody can see does not hold the modal gate.
- Each view is given its host's name, `panel` or `window`, and its questions take it into their identity: the dialog shows "Discard changes?" or "Stash changes", and the identity after the `###` is `ChangesDiscard-<host>` or `ChangesStash-<host>`. `ModalGate` knows the question holding it only by title, so with one title for both, a view that found the gate held under its own title and no popup of its own would hand back the gate another host's open dialog holds.
- What the hosts share lives in `ChangesShared`, owned by `GraphiteLayer` and passed to every host: the message, Amend, and the files remembered for discards in flight.
- `ChangesPanel` alone drains `TakeCommitted` and `TakeDiscarded`, in `OnUpdate`, which runs every frame whether or not the panel is drawn. No other host drains them.

## Rationale

**One drawing, two places.** A copy of the Changes body in the Git window would drift from the panel's at the first fix made to one of them. One class with a callback for the one thing that differs keeps both the same.

**Shared data, not a shared instance.** One instance drawn by two hosts would share its selection and its ImGui ids between two windows. Sharing only the draft gives the reader one message and lets each place keep its own state.

**Writes are drained where it happens every frame.** The side panel's `OnUpdate` already runs on every frame. Each `Take` answers once, so if two hosts drained, the second would get nothing.

## Consequences

### Positive

- The Git window's Changes page is the side panel's, drawn by the same code.
- A message typed in one host shows in the other, and a commit from either clears it in both.
- A discard confirmed in either host is acted on by the panel when it lands, because the remembered files are shared.

### Negative

- Every host must call `WithdrawQuestions` on each frame it does not draw its view. A host that forgets leaves a question holding the modal gate, and nothing in the type says so.
- Draining depends on the side panel being registered. A build of the application without it would stop clearing the draft after a commit and stop warning the editor before a discard.
- A view's question identities are per host, so every host must pass a name no other host uses. Two hosts given the same name would close each other's dialogs again, and nothing checks the names are different.
- The view holds its question titles itself and the questions point into them, so a view can be neither copied nor moved.

### Neutral

- The draft lives as long as the layer, as it did in the panel. It is not saved across a restart.
- The Git window's diff beside the list and a working-tree diff tab still share `GitModel`'s single working-tree diff slot ([0538](0538-refresh-the-working-tree-diff-when-the-repository-changes-not-when-it-is-asked-for.md)), so that host will have to follow the same one-diff rule.

## Alternatives Considered

- **One `ChangesView` drawn by both hosts.** Nothing to share, but the selection and open folders would follow the reader from one place to the other, and ImGui ids would be submitted in two windows on one frame.
- **Each host drains the writes it made.** Keeps a host self-contained, but `GitModel` does not say which host asked for a write, and the panel's draining in `OnUpdate` already runs on every frame.
- **Keep the draft in `GitModel`.** One owner the hosts already hold, but a half-written message is UI state, and the model holds git's data and its freshness ([0491](0491-keep-git-data-in-core-and-git-freshness-in-graphite.md)).

## Related

- [0475](0475-ask-every-modal-question-through-modalquestion.md): the questions each view asks.
- [0515](0515-ask-before-every-discard-in-the-changes-panel.md): the discard question this view asks.
- [0538](0538-refresh-the-working-tree-diff-when-the-repository-changes-not-when-it-is-asked-for.md): the one working-tree diff tab the side panel keeps.
