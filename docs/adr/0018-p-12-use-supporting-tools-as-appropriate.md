# 0018. P.12: Use supporting tools as appropriate

Status: accepted
Date: 2026-08-09
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-09

## Context

The rule this repository fails most plainly. There is `.clang-format` and there
is `warnings "High"` with `fatalwarnings { "All" }`, but no static analyser: no
`.clang-tidy`, no analysis pass in any build configuration.

## Decision

Adopt P.12 as written. Transcribed in full:

> **Reason.** There are many things that are done better "by machine". Computers
> don't tire or get bored by repetitive tasks. We typically have better things
> to do than repeatedly do routine tasks.

**Example.** Run a static analyzer to verify that your code follows the
guidelines you want it to follow.

**Note.** See static analysis tools, concurrency tools, testing tools. There are
many other kinds of tools, such as source code repositories, build tools, etc.,
but those are beyond the scope of these guidelines.

**Note.** Be careful not to become dependent on over-elaborate or
over-specialized tool chains. Those can make your otherwise portable code
non-portable.

## Rationale

Adopted, and recorded here mainly so that the gap it names is written down
rather than merely felt. This directory is about to contain several hundred
rules; without a machine to check any of them, every one of them is advisory,
and the ones that could be checked mechanically are the ones most likely to be
broken silently.

The rule's second Note is the counterweight and it is not rhetorical here: a
clang-tidy setup on a premake-generated MSVC build is a real piece of
configuration to own, and this project has deliberately kept its toolchain
small — see
[0003](0003-vendor-dependencies-as-git-submodules-built-by-premake.md) and
[0005](0005-target-c-20-with-headers-not-modules.md).

Adding a static analyser is therefore *not decided here*. It is a toolchain
decision with its own costs and would be its own record. What is decided is that
its absence is a known gap and not an oversight.

## Consequences

### Positive

- Two tools are already in place and doing real work: `.clang-format` removes
  layout from review entirely, and warnings-as-errors turns a whole class of
  defect into a build failure.
- `Test-Adr.ps1` is this rule applied to the documentation itself — the shape of
  a record is checked by machine rather than by reading.

### Negative

- The most mechanically checkable rules in this directory — bracing, `const`
  correctness, narrowing conversions — are exactly the ones nothing checks. The
  brace rule in [0006](0006-require-braces-around-every-control-flow-body.md)
  ships with 120 known violations for this reason.
- Introducing an analyser later means confronting all of that debt at once,
  which is a strong incentive never to introduce it.

### Neutral

- doctest covers the logic that can be tested headlessly; that half of "testing
  tools" is not the gap.

## Deviations to fix

| What | Where | Note |
|---|---|---|
| No static analyser configured | repository root — no `.clang-tidy` | `readability-braces-around-statements` alone would enforce [0006](0006-require-braces-around-every-control-flow-body.md) |
| Nothing runs the ADR validator automatically | `scripts/Test-Adr.ps1` exists but is invoked by hand | It exits non-zero on a malformed record, so it is ready to be wired into a hook |

## Related

- [0017](0017-p-11-encapsulate-messy-constructs-rather-than-spreading-through-the-code.md)
  — the previous rule.
- [0019](0019-p-13-use-support-libraries-as-appropriate.md) — the next rule.
- [0006](0006-require-braces-around-every-control-flow-body.md) — the rule whose
  enforcement is waiting on this one.
