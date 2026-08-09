# 0038. I.27: For stable library ABI, consider the Pimpl idiom

Status: accepted
Date: 2026-08-09
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-09

## Context

No type in this repository uses Pimpl. The closest thing is
`DirectoryWatcher`, which holds Win32 handles as `void*` members specifically to
keep `Windows.h` out of its header — the same motive as Pimpl, achieved by
type-erasing two members instead of all of them.

## Decision

Adopt I.27 as written, as advice rather than as practice: this project has no
library ABI to stabilise. Transcribed in full:

> **Reason.** Because private data members participate in class layout and
> private member functions participate in overload resolution, changes to those
> implementation details require recompilation of all users of a class that uses
> them. A non-polymorphic interface class holding a pointer to implementation
> (Pimpl) can isolate the users of a class from changes in its implementation at
> the cost of an indirection.

**Example.** interface (`widget.h`)

```cpp
class widget {
    class impl;
    std::unique_ptr<impl> pimpl;
public:
    void draw(); // public API that will be forwarded to the implementation
    widget(int); // defined in the implementation file
    ~widget();   // defined in the implementation file, where impl is a complete type
    widget(widget&&) noexcept; // defined in the implementation file
    widget(const widget&) = delete;
    widget& operator=(widget&&) noexcept; // defined in the implementation file
    widget& operator=(const widget&) = delete;
};
```

implementation (`widget.cpp`)

```cpp
class widget::impl {
    int n; // private data
public:
    void draw(const widget& w) { /* ... */ }
    impl(int n) : n(n) {}
};
void widget::draw() { pimpl->draw(*this); }
widget::widget(int n) : pimpl{std::make_unique<impl>(n)} {}
widget::widget(widget&&) noexcept = default;
widget::~widget() = default;
widget& widget::operator=(widget&&) noexcept = default;
```

**Notes.** See GOTW #100 and cppreference for the trade-offs and additional
implementation details associated with this idiom.

**Enforcement.** (Not enforceable) It is difficult to reliably identify where an
interface forms part of an ABI.

## Rationale

Adopted as advice, not applied. The reason the rule gives — insulating users
from recompilation — has no force in a single-executable build where everything
recompiles together anyway, and the cost it names, an indirection plus a heap
allocation per object, is real.

The second motive for Pimpl, which the rule does not emphasise, does apply here:
keeping a platform header out of a public header. `DirectoryWatcher` needs
exactly that and solves it more cheaply, with two `void*` members rather than a
full implementation object. That solution has its own cost, recorded under
[0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md) — a cast at
every use — and Pimpl is the alternative that would remove those casts at the
price of an allocation.

So this record's practical content is: **if a header ever needs to hide a
platform type from its users, Pimpl is the sanctioned way, and it is preferred
to type-erasing individual members through `void*` when there is more than one
of them.**

## Consequences

### Positive

- Names the sanctioned solution for header hygiene, so the next platform-backed
  type does not have to reinvent the `void*` trick.

### Negative

- Pimpl costs an allocation and an indirection per object, and forces
  constructor, destructor and move operations out of line — five declarations of
  boilerplate before the type does anything.
- Applying it to `DirectoryWatcher` now would be churn for a type that already
  works; the record does not ask for that.

### Neutral

- The ABI-stability motive is inert here for the same reason as in
  [0037](0037-i-26-if-you-want-a-cross-compiler-abi-use-a-c-style-subset.md).

## Related

- [0037](0037-i-26-if-you-want-a-cross-compiler-abi-use-a-c-style-subset.md) —
  the previous rule.
- [0039](0039-i-30-encapsulate-rule-violations.md) — the next rule.
- [0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md) — the `void*`
  members Pimpl would replace.
