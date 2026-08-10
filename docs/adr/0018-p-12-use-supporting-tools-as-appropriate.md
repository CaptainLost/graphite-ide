# 0018. P.12: Use supporting tools as appropriate

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section P (Philosophy), transcribed 2026-08-10

## Context

There are many things that are done better by machine. Computers do not tire or
get bored by repetitive tasks, and there are typically better things to do than
repeat routine ones. The canonical example is running a static analyser to
verify that code follows the guidelines it is supposed to follow — which is
directly relevant to a directory full of transcribed rules, most of which are
otherwise enforced only by review.

The kinds of tool upstream has in mind are static analysis tools, concurrency
tools, and testing tools. Other categories — source control, build systems —
exist but are beyond the scope of the guidelines.

There is a counterweight: be careful not to become dependent on over-elaborate
or over-specialised tool chains. Those can make otherwise portable code
non-portable.

## Decision

Use supporting tools where they can check something a human would otherwise have
to check by hand.

- Treat compiler diagnostics as the first analyser: keep the warning level high
  and keep the build clean, so that a warning means something.
- Prefer a rule that a tool can check to a rule that only review can check, and
  when a convention is adopted, ask what would enforce it.
- Use tests as the mechanism for checking behaviour that no analyser can, and
  keep them cheap enough to run often.
- Keep the tool chain ordinary. A tool that pins the code to one vendor, or that
  must be present for the code to build at all, is a portability cost weighed
  against the checking it buys.

## Rationale

This rule is the one that decides whether the rest of this directory is worth
anything. A convention enforced by review is enforced by whoever happens to be
reviewing, on the day, and decays; the same convention checked by a tool holds
without anyone remembering it. So the useful question when writing a rule is not
"is it right" but "what checks it".

The upstream caution is the reason this is stated as "as appropriate" rather
than "use every tool available". Each tool added is a dependency, a
configuration to maintain, and a way for a build to fail for reasons unrelated
to the code.

## Consequences

### Positive

- Mechanically checkable rules hold consistently, without depending on anyone's
  attention.
- Review time moves to what tools cannot judge: design, naming, whether the
  change is the right change.
- Failures are found at the moment they are introduced.

### Negative

- Every tool is a dependency with a configuration, a version, and its own
  failure modes, and a noisy analyser trains people to ignore its output — the
  worst possible outcome.
- Tool-specific constructs, pragmas and suppressions leak into the source and
  can make it non-portable.
- Tuning and maintaining a tool chain is work that competes with the work being
  checked.

### Neutral

- This record does not adopt any particular analyser, sanitiser or test
  framework; each is its own decision with its own cost.
- Source control and build tooling are out of scope here, as upstream notes.
- Upstream states no enforcement for this rule. It is a disposition; the tools
  that would check the rest of the section are named in the enforcement sections
  of the individual records.

## Related

- [0008](0008-p-2-write-in-iso-standard-c.md) — compiler options that reject
  extensions are the cheapest tool in this category.
- [0019](0019-p-13-use-support-libraries-as-appropriate.md) — the library
  counterpart to this rule.
