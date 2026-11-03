# 0661. Take the open folder through its transitions in one module whose participants decide every step

Status: proposed
Date: 2026-11-03

## Context

About a dozen parts of the application hold something of the open folder:
the language servers' fallback root, the saved state, the documents, the
Claude sessions, the terminals, the Git window, the git port, the diffs and
the reviews. Each has to react when the folder opens, is replaced by another,
closes or moves on disk. The layer did it in three event handlers, sixteen
steps in a fixed order held together by seven WARNING comments, with each
family of windows spelling its part differently.

Nothing made a participant answer every transition. The move handler had been
written for three of them and silently skipped the git port and the servers'
root, so after a rename git read a path that no longer existed. The sequence
lived in `GraphiteLayer`, which has no tests.

## Decision

- `Workspace/FolderTransitions` opens, closes and moves the folder and puts
  together the state saved for it. On opening, every participant lets go of
  the last folder before any arrives in the new one. Trust follows a move
  before any participant does.
- A participant implements `FolderParticipant`: `LetGo` (replaced or closed),
  `Arrive` (root, saved state, restorable state, trust), `FollowMove` and
  `Capture`. All four are pure, so every participant states what it does at
  every step, even when that is nothing.
- Participants are called in the order they are added. That order is the one
  place the sequence is written down: the servers' root, the saved state, the
  documents, the sessions, the terminals, the Git window, the git port, the
  diffs, the reviews.
- The participants are thin adapters in `App/FolderParticipants` over the
  existing classes, which keep their own methods.
- The git models and the Git window still clear themselves on the folder's
  events. Folder toasts are still cleared when an opening starts, and the
  writes on exit stay in `OnDetach`.

## Rationale

The order and the "did everyone follow?" question are the risky part of a
folder transition, and both now sit in one small module that tests can drive
with recording participants. Making every step pure turns a forgotten
participant into a compile error instead of a path nobody reads.

## Consequences

### Positive

- A new participant that saves state or holds the folder's path is one adapter
  and one line of registration, not six edits across the layer.
- The order of open, replace, close and move, and how the saved state is put
  together, are tested without ImGui.

### Negative

- Adapters add a layer between the transitions and the windows they drive; a
  reader follows one more hop to see what a step does.
- Closing the last folder's diffs and reviews now happens before the new
  folder's documents are restored rather than after, a change of order with no
  visible effect.

### Neutral

- The git models' own subscriptions stay, so two wiring styles remain until
  they are moved over too.

## Alternatives Considered

- **Have each feature class implement the participant itself.** Lost for now:
  it would edit `DocumentWindows` and the Git window while both are being
  reworked, for no difference in what the tests can see.
- **Keep the handlers and add the missing steps.** Lost: it fixes the move
  once and leaves the next participant free to be forgotten again.

## Related

- [0522](0522-persist-session-state-in-two-scopes-global-window-and-panels-per-workspace-tabs.md)
  describes what is saved for a folder; this record moves where it is put
  together.
- [0660](0660-carry-a-folder-s-trust-with-it-when-it-moves-while-open.md) is the
  trust step a move now takes first.
