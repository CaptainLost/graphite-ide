# 0634. Give every language-server request a deadline

Status: proposed
Date: 2026-10-25

## Context

The client times out only the startup handshake (30 s) and background analysis (120 s per
file). Once a server is running, a request leaves the pending list only when the server
answers or the client fails. A server that is alive but never answers a request leaves that
request's wait on screen for good:

- "(working...)" in the References panel;
- "counting references…" over a declaration;
- "analysing <file>" in the status bar;
- nothing at all after F12.

Pending requests also keep an idle server from being stopped.

## Decision

- **Start a deadline when a request is written to the server.** A request held back while
  the server starts, or while Roslyn loads projects, has no deadline yet.
- **On expiry, send `$/cancelRequest` and fail the request with the reason "timed out".**
  Every wait then ends the way it ends on any other failure. The reader sees "clangd did
  not answer", with Retry where asking again makes sense.
- **Defaults:**
  - definition, hover and switching header/source: 30 s;
  - references: 120 s;
  - document symbols, semantic tokens and pulled diagnostics: 60 s.
  
  They are named constants in the client.

## Rationale

A wait that cannot end is worse than a failure: it gives the reader nothing to act on. The
limits are long enough for a large solution's Find All References, and short enough that a
stuck server shows up within a coffee sip.

## Consequences

### Positive

- Every language-server wait on screen ends in an answer or a reason.
- Stuck requests stop holding servers alive.

### Negative

- A very slow but working server can be cut off. A large Find All References over 120 s
  reports "did not answer", although the answer would have come.
- Late answers to cancelled requests must be dropped by id, as they already are for
  cancelled tickets.

### Neutral

- The startup handshake keeps its own 30 s limit.

## Alternatives Considered

- **No deadline, show elapsed time.** The reader learns that it is slow, but still has to
  decide when to give up, and the request still pins the server.
- **One deadline for all requests.** References over a large solution need minutes, a
  hover does not. One number is either too short for one or too long for the other.

## Related

- [0633](0633-show-every-wait-with-the-core-loader-and-say-nothing-is-there-only-after-an-answer-said-so.md)
