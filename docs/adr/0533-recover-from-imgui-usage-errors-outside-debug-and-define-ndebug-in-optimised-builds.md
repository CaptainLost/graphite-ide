# 0533. Recover from ImGui usage errors outside Debug and define NDEBUG in optimised builds

Status: proposed
Date: 2026-10-01

## Context

Graphite is heading for a public release, and the build a user runs is Dist.
Until now that build could end itself on a check it did not own:

- Premake does not define `NDEBUG` for an optimised configuration, so the C
  `assert()` stayed on in Release and Dist. ImGui (`IM_ASSERT`), nlohmann/json
  (`JSON_ASSERT`) and pugixml check their invariants through it, and a failed
  check aborts the process. In a windowed build the user sees the IDE vanish.
- ImGui reports a misuse of its API that it can recover from - an unbalanced
  `Begin`/`End` or `PushID`/`PopID`, a cursor moved past the last item without
  submitting one - through `IM_ASSERT_USER_ERROR`, which asserts by default.
  Such a mistake has already shipped once: the changes panel ended a clean
  working tree's list on no item and took the IDE down with it.

Our own checks were never part of this. `CORE_ASSERT` keys off `CORE_DEBUG` and
is compiled out of Release and Dist.

## Decision

- Define `NDEBUG` for the Release and Dist configurations of every project in
  the workspace, at workspace level in `premake5.lua`.
- Route ImGui's recoverable errors through `Core::ImGuiErrorLog`, which
  `ImGuiRenderer` attaches to its context as the context's `ErrorCallback`. It
  writes each distinct message to the engine log once per session, at error
  level, naming the window it first happened in.
- Outside Debug, set `ConfigErrorRecoveryEnableAssert` and
  `ConfigErrorRecoveryEnableTooltip` to false, so ImGui recovers and the log is
  the only trace. In Debug leave both on: the message reaches the log first and
  the assert then breaks into the debugger as before.

## Rationale

A user gains nothing from a process that ends on a mistake the library knows
how to recover from, and loses their unsaved work. The developer still needs to
learn the mistake happened, so it is written down rather than swallowed, and
Debug keeps breaking on it so it is found before it ships.

## Consequences

### Positive

- An ImGui usage error in Dist costs one frame drawn wrongly and one line in
  `graphite.log`, not the process.
- A bug report can carry the log, and the log names the message and the window.

### Negative

- Non-recoverable `IM_ASSERT`s, and the checks in json and pugixml, are now
  compiled out of Release and Dist rather than reported. A broken invariant in
  them goes on silently and may crash later, further from its cause.
- Release no longer behaves like Debug on a vendor check, so a mistake that
  only shows with optimisation on has to be reproduced in Debug to be seen.

### Neutral

- Each message is logged once per session however often it recurs; a repeat
  after the first says nothing new and would flood the log at the frame rate.

## Alternatives Considered

- **Define `IM_ASSERT` through an `IMGUI_USER_CONFIG` header that logs and
  continues.** It would also report ImGui's non-recoverable checks in Dist, but
  it keeps every check evaluated in optimised code - including the bounds check
  in `ImVector::operator[]` on every access - and continuing past one of them
  is undefined behaviour anyway. It also leaves json and pugixml aborting.
- **Leave `assert()` on and only turn ImGui's recovery asserts off.** Smallest
  change, but json and pugixml would still end the process, and a
  non-recoverable `IM_ASSERT` would too.

## Related

- [0004](0004-return-result-t-instead-of-throwing-across-module-boundaries.md)
  - failures our own code expects are values, not asserts.
