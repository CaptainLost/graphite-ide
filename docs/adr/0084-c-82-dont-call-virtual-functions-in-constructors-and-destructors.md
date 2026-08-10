# 0084. C.82: Don't call virtual functions in constructors and destructors

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

The function called will be that of the object constructed so far, rather than a
possibly overriding function in a derived class. This can be most confusing.
Worse, a direct or indirect call to an unimplemented pure virtual function from
a constructor or destructor results in undefined behaviour.

```cpp
class Base {
public:
    virtual void f() = 0;   // not implemented
    virtual void g();       // implemented with Base version
    virtual void h();       // implemented with Base version
    virtual ~Base();        // implemented with Base version
};

class Derived : public Base {
public:
    void g() override;   // provide Derived implementation
    void h() final;      // provide Derived implementation

    Derived()
    {
        // BAD: attempt to call an unimplemented virtual function
        f();

        // BAD: will call Derived::g, not dispatch further virtually
        g();

        // GOOD: explicitly state intent to call only the visible version
        Derived::g();

        // ok, no qualification needed, h is final
        h();
    }
};
```

Calling a specific explicitly qualified function is not a virtual call even if
the function is virtual.

There is nothing inherently wrong with calling virtual functions from
constructors and destructors: the semantics of such calls is type-safe. However,
experience shows that such calls are rarely needed, easily confuse maintainers,
and become a source of errors when used by novices.

## Decision

Do not make an unqualified virtual call from a constructor or a destructor.

- Never call a pure virtual function, directly or through a helper, during
  construction or destruction; that is undefined behaviour, not merely
  surprising.
- Where the intent is to call the version visible in the class being
  constructed, qualify the call — `Derived::g()` — so that the reader can see
  that no dispatch was expected.
- Calling a `final` override needs no qualification, since it cannot dispatch
  further.
- Where derived behaviour is genuinely required during initialisation, move it
  out of the constructor into a post-construction step run by a factory, per
  [0071](0071-c-50-use-a-factory-function-if-you-need-virtual-behavior-during-initialization.md).
- Watch for indirect calls: a non-virtual member function invoked from a
  constructor that itself makes a virtual call is the same mistake, one level
  down.

## Rationale

The rule is not that the behaviour is undefined in general — it is not, and the
guideline says so. The rule is that the call does something precise and
different from what its author almost certainly meant: during `Base`'s
constructor the object's dynamic type is `Base`, so the override that the whole
hierarchy exists to provide is the one thing that will not run.

The pure virtual case is where it stops being a design problem and becomes
undefined behaviour, and nothing in the source of `Derived()` distinguishes the
two cases visually.

## Consequences

### Positive

- A whole class of "my override never ran" bugs disappears from constructors and
  destructors.
- Undefined behaviour from a pure virtual call is ruled out.
- Qualified calls document at the call site which version is intended, which is
  useful even where the unqualified call would have been correct.

### Negative

- Initialisation that legitimately depends on derived state must be restructured
  into a factory and a post-construction step, which forces the object onto the
  free store and adds a partially constructed window — a substantial design cost
  imposed to avoid one call.
- The prohibition is broader than the actual hazard: some virtual calls in
  constructors are well defined and correct, and the rule bans them for the sake
  of maintainers rather than for correctness.
- Indirect calls through helper functions are not visible in the constructor's
  body, so the rule is much harder to check by reading than it looks.

### Neutral

- A qualified call to a virtual function is not a virtual call and is
  unaffected.
- The same reasoning applies to destructors, where the dynamic type has already
  been demoted back towards the base.

## Enforcement

- Flag calls of virtual functions from constructors and destructors.

## Related

- [0071](0071-c-50-use-a-factory-function-if-you-need-virtual-behavior-during-initialization.md)
  — the sanctioned way to get derived behaviour into initialisation.
- [0081](0081-c-67-a-polymorphic-class-should-suppress-public-copy-move.md) —
  another rule about the gap between static and dynamic type in a hierarchy.
- [0013](0013-p-7-catch-run-time-errors-early.md) — the pure virtual call is a
  run-time error the language will not report.
- [0009](0009-p-3-express-intent.md) — the qualified call is how the intent is
  made explicit.
