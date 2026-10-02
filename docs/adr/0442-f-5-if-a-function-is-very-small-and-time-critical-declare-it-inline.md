# 0442. F.5: If a function is very small and time-critical, declare it inline

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section F (Functions), transcribed 2026-08-10

## Context

Some optimisers are good at inlining without hints from the programmer, but do
not rely on it — measure. Over the last forty years or so we have been promised
compilers that can inline better than humans without hints from humans, and we
are still waiting. Specifying `inline`, explicitly or implicitly by writing a
member function inside a class definition, encourages the compiler to do a
better job.

```cpp
inline string cat(const string& s, const string& s2) { return s + s2; }
```

There is an important exception. Do not put an inline function in what is meant
to be a stable interface unless you are certain it will not change: an inline
function is part of the ABI, so its body is baked into every caller that was
compiled against it.

Two further notes limit how often the keyword needs writing at all. `constexpr`
implies `inline`, and member functions defined in-class are inline by default.
Function templates, including member functions of class templates and member
function templates, are normally defined in headers and are therefore inline
already.

## Decision

Reserve `inline` for functions that are both very small and on a measured hot
path.

- Write `inline` on a small free function in a header whose call overhead is a
  meaningful fraction of its work.
- Do not write `inline` on a function already implicitly inline: one defined
  inside a class body, a `constexpr` function, or a template.
- Do not inline anything across a boundary intended to be ABI-stable unless the
  body is certain never to change.
- Measure before treating `inline` as an optimisation; treat it as a hint the
  compiler is free to ignore in either direction.
- Do not use `inline` as a way to get a definition into a header for
  convenience; that is a linkage decision wearing an optimisation's clothes.

## Rationale

The keyword's real cost is not the code bloat everyone quotes, it is that it
publishes the body. Once a function is inline, callers contain a copy, so
changing the implementation requires recompiling them — the ABI argument is what
makes this a design decision rather than a tuning knob. Everything else about
`inline` is advisory: the compiler may inline without it and may decline with
it, which is why the rule is framed around "very small and time-critical" rather
than around any expected speedup.

## Consequences

### Positive

- Call overhead disappears for the small functions where it actually dominates.
- Inlining exposes the body to the caller's optimiser, enabling constant folding
  and dead-branch elimination across the call.
- Decomposition into small functions becomes cheap, so the rule that functions
  stay short costs less.

### Negative

- The function becomes part of the ABI, so any change to its body obliges every
  caller to be rebuilt; at a library boundary this is a lasting constraint.
- The definition must live in the header, which increases compile times for
  every translation unit that includes it and leaks the implementation.
- Aggressive inlining grows code size and can cost more in instruction-cache
  misses than it saves in call overhead — the opposite of the intended effect.
- The keyword is only a hint, so a record that says "declare it inline" cannot
  promise the thing it is being asked for.

### Neutral

- `constexpr` and in-class member definitions already imply `inline`; the
  explicit keyword adds nothing there.
- The header/ABI trade-off only bites where a boundary has to stay stable; a
  pimpl boundary is the alternative when stability matters more than the call
  overhead.

## Enforcement

- Flag inline functions that are more than three statements and could have been
  declared out of line, such as class member functions.

## Related

- [0441](0441-f-4-if-a-function-might-have-to-be-evaluated-at-compile-time-declare-it-constexpr.md)
  — `constexpr` implies `inline`, so the ABI consequence carries there too.
- [0440](0440-f-3-keep-functions-short-and-simple.md) — the rule whose call
  overhead this one answers.
- [0038](0038-i-27-for-stable-library-abi-consider-the-pimpl-idiom.md) — the
  countermeasure when a boundary must stay stable.
- [0015](0015-p-9-do-not-waste-time-or-space.md) — including the requirement to
  measure before optimising.
