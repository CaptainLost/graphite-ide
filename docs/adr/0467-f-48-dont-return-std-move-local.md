# 0467. F.48: Don't return std::move(local)

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section F (Functions), transcribed 2026-08-10

## Context

With guaranteed copy elision, it is now almost always a pessimisation to use
`std::move` expressly in a return statement.

```cpp
// bad
S f()
{
  S result;
  return std::move(result);
}
```

```cpp
// good
S f()
{
  S result;
  return result;
}
```

Returning the named local directly permits named return value optimisation: the
object is constructed in the caller's storage and no move happens at all.
Wrapping it in `std::move` produces an xvalue rather than the name of a local,
which disqualifies the optimisation and forces a move construction that the
plain form would have skipped.

## Decision

Return a local object by naming it, not by moving it.

- Write `return result;` where `result` is a local of the function's return
  type. Do not write `return std::move(result);`.
- Rely on the implicit move that the language already applies when the returned
  name is a local or a by-value parameter; the compiler does this without being
  asked.
- Do not write `std::move` on a returned temporary either; there is no object to
  move from that outlives the expression.
- Keep `std::move` for what it is for: handing a named object to something that
  will take it, at a point where the object will not be used again.
- A returned member, a returned reference parameter, or a returned object of a
  different type from the return type are not covered by this rule; those may
  legitimately need an explicit move, and each is a deliberate decision rather
  than a habit.

## Rationale

The rule earns its place because the wrong form looks like the more careful one.
`std::move` reads as an optimisation, so it is added by people trying to be
efficient, and it makes the function measurably slower in exactly the cases
where the object is expensive enough for anyone to care.

Guaranteed copy elision and implicit move mean the language has already made the
best choice available; writing `std::move` overrides that choice with a worse
one, and does so silently, with no diagnostic and no visible symptom other than
a move that need not have occurred.

## Consequences

### Positive

- Named return value optimisation stays available, so the common case costs
  nothing at all rather than one move.
- The return statement is shorter and says exactly what it does.
- It removes a false signal: a reader who sees `std::move` stops reading and
  asks why, and here there is no answer.

### Negative

- The rule sounds absolute but is not. `return std::move(member);` and
  `return std::move(x);` where `x` is of a type convertible to, but not the same
  as, the return type can be correct, so the checkable version of the rule is
  narrower than its title.
- Elision is guaranteed for temporaries but named return value optimisation is
  only permitted, not required, so on an unhelpful compiler or in an unoptimised
  build the plain form may still perform the move this rule claims to avoid.
- The cost the rule removes is a single move construction, invisible in the
  program's behaviour and its output, so nothing in a normal build reveals a
  violation. Catching it reliably needs the tooling named below; without a
  configured checker the rule holds by review alone.

### Neutral

- The rule is a consequence of the C++17 language change; older codebases were
  written under different advice, and encountering the old form is not evidence
  that anyone was careless.
- It says nothing about `std::forward`, which is governed by the
  perfect-forwarding rules and has a different reason to appear in a return
  expression.

## Enforcement

- This should be enforced by tooling, by checking the return expression.

## Related

- [0464](0464-f-45-dont-return-a-t.md) — the other way an attempt to avoid a copy
  in a return statement goes wrong.
- [0462](0462-f-43-never-directly-or-indirectly-return-a-pointer-or-a-reference-to-a-local-object.md)
  — returning a local by value is the safe alternative it recommends.
- [0015](0015-p-9-do-not-waste-time-or-space.md) — the pessimisation this rule
  removes.
- [0018](0018-p-12-use-supporting-tools-as-appropriate.md) — a static analysis
  check finds this reliably, and a reviewer does not.
