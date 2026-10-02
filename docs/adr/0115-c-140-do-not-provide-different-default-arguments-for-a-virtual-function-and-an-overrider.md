# 0115. C.140: Do not provide different default arguments for a virtual function and an overrider

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

Differing defaults cause confusion, because an overrider does not inherit
default arguments. A default argument is chosen from the static type of the
expression the call is written through, while the function body that runs is
chosen from the dynamic type of the object. Put a different default on the
overrider and the two halves of the call disagree.

```cpp
class Base {
public:
    virtual int multiply(int value, int factor = 2) = 0;
    virtual ~Base() = default;
};

class Derived : public Base {
public:
    int multiply(int value, int factor = 10) override;
};

Derived d;
Base& b = d;
b.multiply(10);  // these two calls will call the same function but
d.multiply(10);  // with different arguments and so different results
```

Both calls dispatch to `Derived::multiply`. One of them multiplies by two and
the other by ten, and nothing at either call site says so; the difference is
carried entirely by the declared type of the name the call went through.

## Decision

Do not give an overrider a default argument that differs from the one on the
function it overrides.

- Declare a default argument for a virtual function in one place only, on the
  base declaration, and let every overrider take the full parameter list.
- Do not repeat the base's default on an overrider even when the value is the
  same; a copy is a value that can drift.
- Where a default genuinely belongs to the interface, prefer a non-virtual
  public function that carries the default and forwards to a virtual function
  that has none, so that the default is not part of the overridable surface at
  all.
- When removing a default from an overrider, check every call made through the
  derived type, because those calls will stop compiling or silently change
  meaning.

## Rationale

The point of a virtual function is that behaviour follows the object. A default
argument on an overrider breaks that promise halfway: the body follows the
object and the argument follows the handle, so the same call on the same object
produces different results through a `Base&` and through a `Derived&`. That is
not a subtle difference in a corner case, it is an ordinary call site reading
one way and behaving another, and no amount of care at the call site reveals
it.

## Consequences

### Positive

- A virtual call means the same thing regardless of which type in the hierarchy
  the caller happens to be holding.
- There is exactly one place to read, and one place to change, the default for
  any given virtual function.
- The mechanical form of the rule — no defaults on overriders — is checkable
  without understanding the code.

### Negative

- The rule does not remove the underlying hazard, it only removes the visible
  symptom: a base-declared default is still statically bound, so it is still
  part of the caller's compiled code and still cannot be changed without
  rebuilding callers.
- Overriders lose the convenience of a short call through the derived type, so
  derived-side call sites become more verbose.
- The non-virtual forwarding form fixes the problem properly but adds a
  function to every such interface, which is real ceremony for what looked like
  a one-line default.
- Identical defaults on both declarations are legal under the rule as stated
  upstream, and that is exactly the state a later edit turns into a differing
  pair.

### Neutral

- The rule says nothing about whether a virtual function should have a default
  argument at all; it constrains only where the default is written.

## Enforcement

- Flag default arguments on virtual functions if they differ between the base
  and derived declarations.

## Related

- [0116](0116-c-145-access-polymorphic-objects-through-pointers-and-references.md)
  — the other rule about the gap between an object's static and dynamic type.
- [0124](0124-c-153-prefer-virtual-function-to-casting.md) — virtual dispatch is
  trusted precisely because it follows the object, which this rule protects.
- [0009](0009-p-3-express-intent.md) — a call whose meaning depends on the
  declared type of the handle does not express its intent.
- [0020](0020-i-1-make-interfaces-explicit.md) — the differing default is state
  the call site cannot see.
