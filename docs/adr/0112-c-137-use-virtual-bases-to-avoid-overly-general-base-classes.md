# 0112. C.137: Use virtual bases to avoid overly general base classes

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

Virtual bases allow separation of shared data and interface, and avoid all
shared data being pushed into an ultimate base class.

```cpp
struct Interface {
    virtual void f();
    virtual int g();
    // ... no data here ...
};

class Utility {  // with data
    void utility1();
    virtual void utility2();    // customization point
public:
    int x;
    int y;
};

class Derive1 : public Interface, virtual protected Utility {
    // override Interface functions
    // Maybe override Utility virtual functions
    // ...
};

class Derive2 : public Interface, virtual protected Utility {
    // override Interface functions
    // Maybe override Utility virtual functions
    // ...
};
```

Factoring out `Utility` makes sense if many derived classes share significant
implementation details. `Interface` is the root of an interface hierarchy and
`Utility` is the root of an implementation hierarchy; the example is admittedly
theoretical, as it is hard to find a small realistic one.

The pressure the rule relieves is this: without a separate `Utility`, the shared
implementation would have to go into `Interface`, which would then carry data
and stop being an interface — the mixture
[0104](0104-c-129-when-designing-a-class-hierarchy-distinguish-between-implementation-inheritance-and-interface-inheritance.md)
warns against. Splitting them and inheriting from both keeps the interface
data-free.

`virtual` on the `Utility` base is what makes the split survive further
derivation. If a class later derives from both `Derive1` and `Derive2`, a
non-virtual `Utility` would appear twice, giving two copies of `x` and `y`;
declaring it virtual means there is exactly one shared sub-object however many
paths lead to it. `protected` keeps it out of the public interface.

Often, linearisation of a hierarchy is a better solution than a virtual base.

## Decision

Where an interface hierarchy and an implementation hierarchy meet, use a virtual
base rather than widening the interface.

- Keep the interface base free of data, and put shared implementation in a
  separate base.
- Inherit the implementation base virtually, and normally non-publicly, so that
  it is shared rather than duplicated and does not become part of the derived
  class's interface.
- Do this only where several derived classes genuinely share significant
  implementation; for one or two, a member is simpler.
- Remember that a virtual base is initialised by the most-derived class, not by
  its intermediate bases, and write the constructors accordingly.
- Consider linearising the hierarchy first — flattening it into a chain, or
  replacing the shared base with a member — and reach for a virtual base only
  when that does not work.

## Rationale

The alternative this rule displaces is the ultimate base class: a single root
that accumulates every piece of shared data because there is nowhere else to put
it. That root becomes untouchable, since changing it recompiles and relayouts
the whole hierarchy, and it forces every derived class to carry state it does
not use.

The `virtual` keyword itself is doing something specific and easily overlooked:
it makes the base sub-object shared rather than duplicated. Without it, the
whole arrangement works only until two of these classes are combined, and then
silently yields two copies of the shared state.

## Consequences

### Positive

- The interface stays free of data even though implementations share state.
- Shared implementation exists once per object, not once per inheritance path.
- Adding another derived class costs nothing in the interface.

### Negative

- A virtual base is not free: access to it goes through an offset stored in the
  object, so member access is an indirection and the object is larger.
- Initialisation order changes in a way that surprises: the most-derived class
  initialises the virtual base, so an intermediate class's member initialiser
  for it is ignored when that class is not the most derived.
- `dynamic_cast` is required to cast down from a virtual base; `static_cast`
  will not compile.
- The rule's own note concedes that linearisation is often better, so this is a
  technique with a narrow band of applicability and a wide band of misuse.

### Neutral

- The upstream example is acknowledged as theoretical; the pattern is aimed at
  large hierarchies where a small illustration cannot show the benefit.
- The dual-hierarchy construction in
  [0104](0104-c-129-when-designing-a-class-hierarchy-distinguish-between-implementation-inheritance-and-interface-inheritance.md)
  is the same mechanism applied systematically.

## Enforcement

- Flag mixed interface and implementation hierarchies.

## Related

- [0104](0104-c-129-when-designing-a-class-hierarchy-distinguish-between-implementation-inheritance-and-interface-inheritance.md)
  — the separation this rule implements.
- [0111](0111-c-136-use-multiple-inheritance-to-represent-the-union-of-implementation-attributes.md)
  — where the colliding implementation bases come from.
- [0101](0101-c-126-an-abstract-class-typically-doesnt-need-a-user-written-constructor.md)
  — the record that warns data in an abstract base leads here.
- [0108](0108-c-133-avoid-protected-data.md) — `virtual protected Utility` still
  has to keep its data private.
