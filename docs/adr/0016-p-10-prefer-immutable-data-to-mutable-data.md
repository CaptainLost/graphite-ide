# 0016. P.10: Prefer immutable data to mutable data

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section P (Philosophy), transcribed 2026-08-10

## Context

It is easier to reason about constants than about variables. Something immutable
cannot change unexpectedly. Sometimes immutability enables better optimisation.
And there cannot be a data race on a constant.

Upstream, this rule is a pointer: the substance lives in the section on
constants and immutability, and P.10 exists to state the default. It is recorded
here for the same reason — so that the default is written down and can be cited,
rather than reconstructed each time from the rules that depend on it.

## Decision

Prefer immutable data to mutable data. `const` is the default; mutability is
what requires a reason.

- Declare a local `const` unless it is assigned after initialisation.
- Take function parameters by reference to `const` unless the function is meant
  to modify the argument; a non-`const` reference parameter is a statement that
  it does.
- Make a member function `const` unless it modifies the object.
- Prefer returning a new value to mutating one in place, where the cost is not
  the point.
- Where a value is known at compile time, prefer `constexpr` to `const`.

## Rationale

The whole benefit is in what a reader does not have to do. A `const` binding is
a fact that holds for the rest of the scope; a mutable one is a question to be
answered by reading everything in between. In concurrent code that difference is
not merely convenience: a value that cannot change cannot be raced on, and needs
no synchronisation at all.

Making immutability the default rather than an option is what makes it useful.
If `const` appears only where someone thought to add it, its absence says
nothing.

## Consequences

### Positive

- Fewer things can change, so less has to be tracked while reading.
- Data races on constants are impossible, which removes synchronisation
  questions rather than answering them.
- The compiler occasionally optimises better, and at minimum diagnoses
  accidental modification.

### Negative

- `const` is viral: a `const` member function can only call the `const` member
  functions of its own object, and correcting a chain of them is a diff far
  larger than the change that prompted it.
- A `const` data member makes a class non-assignable — both copy and move
  assignment are implicitly deleted — which is a trap that appears far from the
  declaration.
- Preferring new values to in-place mutation costs copies where mutation would
  have been free, which pulls against
  [0015](0015-p-9-do-not-waste-time-or-space.md).

### Neutral

- The detailed rules — where `constexpr` beats `const`, what to do about `mutable`
  and logical constness — are the subject of the upstream constants and
  immutability section, not of this record.
- This record states a default, not a prohibition on mutable state.

## Related

- [0007](0007-p-1-express-ideas-directly-in-code.md) — `const` as part of the
  contract a declaration states.
- [0009](0009-p-3-express-intent.md) — why the range-`for` binds by reference to
  `const` by default.
- [0015](0015-p-9-do-not-waste-time-or-space.md) — the tension this rule has to
  be weighed against.
