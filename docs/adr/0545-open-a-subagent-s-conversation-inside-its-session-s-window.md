# 0545. Open a subagent's conversation inside its session's window

Status: proposed
Date: 2026-10-01

## Context

An expanded Agent call in a session's Conversation showed only what the subagent
cost. The subagent's own transcript is `<session>/subagents/agent-<id>.jsonl`, and
it is tied to the call that started it by the `toolUseId` in
`agent-<id>.meta.json`. That link exists for 502 of the 503 Agent calls in the
local transcripts, so it can be relied on and a missing one treated as an
exception.

The Conversation view is virtualised by row, and a window has three tabs
(Conversation, Cost, Requests) that all read the same transcript.

## Decision

An Agent call whose link is known offers an "Open conversation" button. It
switches the session's window to the subagent: all three tabs read the subagent's
transcript, under a breadcrumb row naming the session and the subagent.

The arrow, the session's title, Esc while the window has the keyboard and nothing
in it is being edited, and the mouse's back button all return to the session. The
session's own views are left untouched while the subagent is shown, so going back
finds the scroll position and expanded rows where the reader left them. The session
keeps being read meanwhile.

One level only: a subagent's own Agent calls offer no button. The Cost tab of a
subagent leaves out the split between the session and its subagents, which it does
not have. Which subagent is open is not remembered across launches.

## Rationale

The reader is already in the session's window and the three tabs already answer
"what did this transcript do, what did it cost, what requests did it make"; reusing
them for the subagent costs nothing new to learn. Leaving the session's views
alone makes the return trip exact.

## Consequences

### Positive

- A subagent's conversation, cost and requests are one click from the call that
  started it, and one keypress back.
- No second set of tabs or windows to manage; the session's state survives the
  detour.

### Negative

- The session and one of its subagents cannot be read side by side.
- A subagent of a subagent is out of reach from the UI.
- Closing and reopening Graphite returns to the session, not the subagent.

### Neutral

- The subagent's transcript is read by its own `LiveTranscript` for as long as it
  is shown, and dropped when the reader goes back.

## Alternatives Considered

- **A separate window per subagent.** Allows reading side by side, but opens one
  tab per agent, and a session can start dozens.
- **Expanding the subagent inline under its call.** One scroll for the whole run,
  but a long subagent swamps the session's conversation, and the view is
  virtualised by row, so a nested conversation does not fit its measuring.
