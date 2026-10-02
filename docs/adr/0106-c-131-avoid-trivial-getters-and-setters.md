# 0106. C.131: Avoid trivial getters and setters

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

A trivial getter or setter adds no semantic value; the data item could just as
well be public.

```cpp
class Point {   // Bad: verbose
    int x;
    int y;
public:
    Point(int xx, int yy) : x{xx}, y{yy} { }
    int get_x() const { return x; }
    void set_x(int xx) { x = xx; }
    int get_y() const { return y; }
    void set_y(int yy) { y = yy; }
    // no behavioral member functions
};
```

Consider making such a class a `struct` — that is, a behaviourless bunch of
variables, all public data and no member functions.

```cpp
struct Point {
    int x {0};
    int y {0};
};
```

Note that default initialisers can be put on member variables, so the `struct`
form does not lose the guarantee that the members start with a known value.

The key to the rule is whether the semantics of the getter or setter are
trivial. While it is not a complete definition of "trivial", consider whether
there would be any difference beyond syntax if the getter or setter were a
public data member instead. Examples of non-trivial semantics are maintaining a
class invariant, and converting between an internal type and an interface type.

The point is not that encapsulation is unimportant, but that a pair of trivial
accessors provides none of it. The member is readable and writable by anyone, at
any time, to any value — exactly as if it were public — while costing four
declarations, four call sites' worth of noise, and the false impression that the
class is enforcing something.

## Decision

Do not write a getter and setter pair that only reads and writes a member.

- If a class has no invariant across its members and no behaviour, make it a
  `struct` with public data and default member initialisers.
- Write an accessor only when it does something a public member would not:
  enforce or re-establish an invariant, convert between an internal
  representation and the interface type, apply a unit or a range check, notify
  an observer, or provide a stable interface across an ABI boundary.
- Where only reading is meaningful, provide the getter alone and keep the member
  private; a read-only accessor is not trivial in the same sense, because there
  is no public-member equivalent.
- Do not add accessors speculatively, on the grounds that behaviour might be
  needed later; add them when the behaviour arrives.

## Rationale

The test the rule offers is a good one precisely because it is mechanical: ask
whether replacing the accessor pair with a public member would change anything
other than the spelling at the call sites. If the answer is no, the accessors
are ceremony.

The cost is not only verbosity. A class with trivial accessors advertises an
encapsulation it does not have, so a reader has to check each accessor to
discover that none of them enforces anything, and a maintainer may assume an
invariant exists where none does.

## Consequences

### Positive

- Classes with no invariant become short, obvious aggregates.
- The presence of an accessor becomes informative: it means something is
  happening.
- Aggregate initialisation and structured bindings become available on the
  `struct` form.

### Negative

- Making a member public is an interface commitment that is expensive to
  reverse: adding an invariant later means changing every call site, whereas the
  accessor pair could have absorbed it in place.
- A public member cannot be given a breakpoint, a log line, or a deprecation
  attribute on write, so debugging and migration lose a hook.
- Across an ABI boundary a public member fixes the layout, so this rule and
  [0100](0100-c-122-use-abstract-classes-as-interfaces-when-complete-separation-of-interface-and-implementation-is-needed.md)
  pull in different directions for types that cross one.
- "Trivial" is deliberately not fully defined, so the rule needs judgement at
  the margin.

### Neutral

- The rule is about accessor pairs on data with no invariant; it says nothing
  against a rich interface over private state.
- The convention of declaring such a type `struct` rather than `class` is a
  signal to the reader, not a language distinction.

## Enforcement

- Flag multiple get and set member functions that simply access a member without
  additional semantics.

## Related

- [0109](0109-c-134-ensure-all-non-const-data-members-have-the-same-access-level.md)
  — the same distinction between a bundle of values and a type with an
  invariant.
- [0107](0107-c-132-dont-make-a-function-virtual-without-reason.md) — another
  rule against machinery added by reflex.
- [0015](0015-p-9-do-not-waste-time-or-space.md) — accessors that do nothing are
  waste of a reader's time.
- [0007](0007-p-1-express-ideas-directly-in-code.md) — a `struct` says "bundle
  of values" directly.
