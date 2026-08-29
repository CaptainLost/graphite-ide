# 0509. Write the terminal emulator rather than adopt one

Status: proposed
Date: 2026-08-29

## Context

Graphite had no way to run a command. `ConsolePanel` is a log viewer, not a
terminal — it has no input. `Core::ChildProcess` talks to its child through
pipes, which is exactly right for a language server and exactly wrong for a
shell: a program that detects its stdout is a pipe turns its colour off, turns
its prompt off, and stops behaving as an interactive program. The bar set for
the feature was parity with the system console — whatever a command shows in a
PowerShell window, the panel shows the same thing.

Meeting that bar needs two things a pipe cannot give: a pseudo-console
(ConPTY) on the process side, so the child believes it is talking to a real
terminal, and something on this side that turns the byte stream ConPTY hands
back — an interleaving of printable text and VT/ANSI escape sequences — into a
grid of cells with a cursor, colours and scrollback.

## Decision

The VT emulator is written here, in a new `Core/Term` module, rather than
adopted from a library such as `libvterm`. `TerminalScreen` holds the cell
grid, the cursor, the scrollback and the alternate buffer; `VtParser` drives it
from a byte stream and emits reply bytes where the protocol requires them. The
process side is `Core::PseudoConsole`, a ConPTY sibling of `Core::ChildProcess`
in `Core/Platform`, following the same `#ifdef _WIN32` / `#else` returning
`ErrorCode::Unsupported` convention as every other file there.

## Rationale

Three reasons carried this, in order of weight. The emulator is the only part
of the feature that is pure logic, and `CoreTests` is the only place in this
repository where anything is tested; handing it to a library would move the
whole feature into the untested half of the codebase, where correctness rests
on running the app and looking. The cell model is what the panel reaches into
directly — selection over cells, scrollback joined for copy, palette indices
resolved against the active scheme — and a foreign cell model is one the panel
adapts to rather than one that serves it. And the prior art points the same
way without endorsing the adopt-a-library alternative: VS Code emulates with
`xterm.js` and spawns with `node-pty`; JetBrains emulates with `JediTerm` and
spawns with `pty4j`. Both wrote their own emulator having had the option not
to, and both drew the same line this decision draws, between the process layer
and the emulation layer. Neither wrote its own ConPTY.

## Consequences

### Positive

- The emulator is covered by roughly ninety headless test cases in
  `CoreTests`, exercising the parser's state machine, UTF-8 decoding, SGR and
  mode handling, and the screen's scrolling and alternate-buffer behaviour —
  none of it needs a window or a real shell to run.
- The process/emulation split is one file wide. `PseudoConsole` is the only
  place that talks to ConPTY; a `forkpty`-based sibling for a POSIX build is
  additive, not a rewrite of `VtParser` or `TerminalScreen`.
- The cell model serves the panel directly: selection, copy and colour
  resolution all read `TerminalScreen`'s own types, with nothing to adapt.

### Negative

- Sequence coverage is open-ended work. Every program that emits something the
  parser does not yet handle is a gap that surfaces as visibly wrong output,
  not a build failure, and closing it recurs for as long as the feature is
  used against real-world shells and TUIs.
- The teardown path produced two separate deadlocks during implementation, one
  in each of the two new classes: `PseudoConsole`'s destructor closed the pipe
  before calling `ClosePseudoConsole`, so ConPTY's own flush had no reader
  draining it; `TerminalSession`'s destructor raised its stopping flag outside
  the mutex the reader thread waits under, a lost wakeup that hangs `join()`
  whenever the queue is at capacity when a tab closes. Both are exactly the
  class of defect that writing the process and emulation layers by hand buys:
  nobody hands you a hardened shutdown sequence for free. The lost-wakeup fix
  is not covered by a regression test — reverting it reddens no case, because
  the race window is the gap between the reader evaluating its wait predicate
  and atomically releasing the mutex to sleep, nanoseconds wide, and closing it
  would mean cutting a synchronisation seam into production shutdown code
  purely to make the window reproducible. That trade was judged worse than the
  gap: the fix stands on review and a written argument, not on a test, and a
  future change to that code carries this record and the comments beside it as
  its only defence.
- The ANSI 0–15 colour slots use standard xterm values and deliberately do not
  follow the active colour scheme, though the design this record supersedes
  said they would. The scheme's own colours at those positions are translucent
  highlight bands meant to sit behind text, not opaque foreground colours;
  forcing them opaque would invent values the scheme never offered. Only the
  default foreground and background are drawn from the scheme. The terminal
  therefore does not yet belong to the active theme the way the design
  promised; buying that back needs sixteen named terminal colours added to the
  scheme, which is additive and can be done without revisiting this decision.

### Neutral

- Left deliberately out of scope: mouse reporting, `OSC 8` hyperlinks, reflow
  of wrapped lines on resize, user-declared shell profiles beyond what
  `ShellProfiles::DiscoverShells()` finds automatically, and the `forkpty`
  branch of `PseudoConsole`.

## Alternatives Considered

### Adopt a VT library

Vendor `libvterm` as a submodule under `vendor`, per ADR 0003, and write only
the pseudo-console, shell discovery and the panel around it. This buys
sequence-coverage completeness for free, which is the expensive part of the
alternative chosen above. Rejected because it moves the one part of the
feature that could be tested headless into a dependency this repository does
not test, and because its cell model is not the one the panel would have
been designed around, making every panel operation an adaptation rather than a
direct read.

## Related

- [0002](0002-keep-core-free-of-ide-knowledge.md) — the module lives in Core,
  under the same constraint: `Core/Term` and `Core/Platform` know nothing of
  `TerminalPanel` or any other Graphite type.
- `docs/superpowers/specs/2026-08-29-terminal-design.md` — the design this
  record's decision is drawn from, and which its Negative section partly
  supersedes on the ANSI colour question.
