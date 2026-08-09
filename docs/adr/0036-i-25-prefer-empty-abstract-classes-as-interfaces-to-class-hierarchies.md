# 0036. I.25: Prefer empty abstract classes as interfaces to class hierarchies

Status: accepted
Date: 2026-08-09
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-09

## Context

Four abstract bases exist in Core. Three are pure interfaces with no data:
`Window`, `ProjectContentReader` and `SolutionEditor`. One is not: `Panel`
carries `m_id`, `m_title` and `m_visible`, and gives its virtuals default
implementations rather than making them pure.

## Decision

Adopt I.25 as written. Transcribed in full:

> **Reason.** Abstract classes that are empty (have no non-static member data)
> are more likely to be stable than base classes with state.

**Example, bad.** You just knew that `Shape` would turn up somewhere :-)

```cpp
class Shape {  // bad: interface class loaded with data
public:
    Point center() const { return c; }
    virtual void draw() const;
    virtual void rotate(int);
    // ...
private:
    Point c;
    vector<Point> outline;
    Color col;
};
```

This will force every derived class to compute a center — even if that's
non-trivial and the center is never used. Similarly, not every `Shape` has a
`Color`, and many `Shape`s are best represented without an outline defined as a
sequence of `Point`s. Using an abstract class is better:

```cpp
class Shape {    // better: Shape is a pure interface
public:
    virtual Point center() const = 0;   // pure virtual functions
    virtual void draw() const = 0;
    virtual void rotate(int) = 0;
    // ...
    // ... no data members ...
    // ...
    virtual ~Shape() = default;
};
```

**Enforcement.** (Simple) Warn if a pointer/reference to a class `C` is assigned
to a pointer/reference to a base of `C` and the base class contains data
members.

## Rationale

Adopted as written for interfaces — the types whose purpose is to be
implemented elsewhere and held by pointer. `Window`, `ProjectContentReader` and
`SolutionEditor` already satisfy it, and that is not accidental: `Window` being
data-free is what makes the GLFW substitution in
[0002](0002-keep-core-free-of-ide-knowledge.md) a one-file change.

`Panel` is a deliberate exception and is recorded as one rather than quietly
tolerated. It is not an interface in the rule's sense; it is a base class
providing shared behaviour every panel needs — an identity, a title and a
visibility flag that `PanelManager` reads generically. Making it empty would
push those three members into every derived panel and give `PanelManager`
nothing to work with. The rule's own complaint does not land: no derived panel
is forced to compute something it does not have, because all three members are
things every panel genuinely has.

The distinction this record draws: **a type meant for substitution is empty; a
type meant for reuse may carry state, and is not called an interface.**

## Consequences

### Positive

- Three of the four abstract types are already pure, and the record fixes that
  as the expectation for anything new that exists to be swapped.
- Names the `Panel` case explicitly, so the next reader does not have to decide
  whether it is an oversight.

### Negative

- The distinction between "interface" and "shared base" is a judgement, and this
  record does not make it mechanical. A future base class with state can be
  justified the same way `Panel` is, correctly or not.
- `Panel`'s state means every panel pays for three members whether or not it
  uses them, which is the rule's objection in miniature — small here, but real.

### Neutral

- The enforcement point is a static-analysis check with no tool to run it; see
  [0018](0018-p-12-use-supporting-tools-as-appropriate.md).

## Deviations to fix

| What | Where | Note |
|---|---|---|
| `Panel` is an abstract base carrying `m_id`, `m_title`, `m_visible` | `Core/src/Core/Ui/Panel.h:12` | Argued above as a shared base rather than an interface. Recorded so the exception is visible, not because a change is planned |

## Related

- [0035](0035-i-24-avoid-adjacent-parameters-that-can-be-invoked-by-the-same-arguments-in-either-order-with-different-meaning.md)
  — the previous rule.
- [0037](0037-i-26-if-you-want-a-cross-compiler-abi-use-a-c-style-subset.md) —
  the next rule.
- [0002](0002-keep-core-free-of-ide-knowledge.md) — the substitution `Window`'s
  emptiness makes cheap.
