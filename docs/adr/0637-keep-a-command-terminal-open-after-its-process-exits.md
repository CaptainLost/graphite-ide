# 0637. Keep a command terminal open after its process exits

Status: proposed
Date: 2026-10-25

## Context

Every terminal tab closed itself on the frame after its process ended. For an interactive
shell that is what typing `exit` means. A terminal opened to run one command behaves the
same way, for example "New Claude session" or "Continue in terminal". When the command fails
at once (an auth error, a session it cannot resume), its tab vanished with the error text.
The "New Claude session" tab kept waiting for a session that would never be written.

## Decision

- **A terminal opened for a command stays open after its process exits.** It shows a
  final dimmed line, "Process exited with code N", and marks its tab as exited. The reader
  closes it.
- **An interactive shell tab still closes when its shell exits.**
- **A pending "New Claude session" tab is tied to its terminal.** If the process exits
  before the session's first lines appear, the tab stops waiting. It says Claude exited
  before starting a session, with the exit code, and offers to show the terminal.

## Rationale

A command's output is most needed exactly when the command failed, which is also when it
ends soonest.

## Consequences

### Positive

- Failed commands leave their error on screen.
- No session tab waits forever on a process that is gone.

### Negative

- Finished command terminals accumulate until the reader closes them.

### Neutral

- Shell tabs behave as before.

## Related

- [0633](0633-show-every-wait-with-the-core-loader-and-say-nothing-is-there-only-after-an-answer-said-so.md)
