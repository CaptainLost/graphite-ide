# 0062. C.41: A constructor should create a fully initialized object

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

A constructor establishes the invariant for a class. A user of a class should be
able to assume that a constructed object is usable.

```cpp
class X1 {
    FILE* f;   // call init() before any other function
    // ...
public:
    X1() {}
    void init();   // initialize f
    void read();   // read from f
    // ...
};

void f()
{
    X1 file;
    file.read();   // crash or bad read!
    // ...
    file.init();   // too late
    // ...
}
```

Compilers do not read comments. The requirement to call `init()` first is
expressed in a place nothing can check, and the failure appears as a crash or a
bad read at a call the compiler was perfectly happy with.

If a valid object cannot conveniently be constructed by a constructor, use a
factory function.

If a constructor acquires a resource in order to create a valid object, that
resource should be released by the destructor. The idiom of having constructors
acquire resources and destructors release them is called RAII, Resource
Acquisition Is Initialization.

## Decision

Leave nothing for the caller to do after construction.

- Initialise every member in every constructor, whether explicitly, through a
  delegating constructor, or by default construction.
- Do not write an `init()`, `initialize()`, `setup()` or `open()` that must be
  called before the object is usable, and do not document such an ordering in a
  comment.
- Where the object cannot be built directly, use a factory function that returns
  either a fully constructed object or a failure, per
  [0063](0063-c-42-if-a-constructor-cannot-construct-a-valid-object-throw-an-exception.md).
- Where the constructor acquires a resource, ensure the destructor releases it,
  per
  [0055](0055-c-31-all-resources-acquired-by-a-class-must-be-released-by-the-classs-destructor.md).
- Where the constructor is meant to establish an invariant, express it as an
  `Ensures` so that the postcondition is stated rather than assumed.

## Rationale

Two-phase initialisation multiplies the states a type can be in, and every
member function must then handle the extra one. The type stops having an
invariant, because there is no point at which the condition is guaranteed; what
it has instead is a protocol, and protocols are enforced by the caller reading
the documentation.

The `X1` example is the argument in one line: `file.read()` before `file.init()`
compiles, and the type gave the compiler nothing to object to. A constructor
that fully initialises makes the same mistake unrepresentable, because there is
no moment between the object existing and the object being usable.

## Consequences

### Positive

- Every object is usable from its first statement, so no member function has to
  handle an uninitialised state.
- Misuse of the type becomes impossible rather than merely documented.
- Resource acquisition and release are paired by scope, which is what makes RAII
  work.

### Negative

- A constructor cannot report a failure by returning, so this rule forces
  fallible construction into either an exception or a factory function, and the
  factory route means a private constructor, a static function and a move on
  return — considerably more code than an `init()`.
- Doing all the work up front removes the option of deferring expensive
  initialisation, so a type that could have been cheap to create and expensive
  to use becomes expensive to create.
- The rule is at odds with types whose construction is genuinely staged, such as
  a UI panel that cannot be laid out before its parent exists; those end up
  either with a factory or with the very state machine this rule rejects.

### Neutral

- The rule concerns the object being usable, not every member holding a
  meaningful value; a container that begins empty is fully initialised.
- Factory functions are explicitly the sanctioned escape rather than an
  exception to the rule.

## Enforcement

- (Simple) Every constructor should initialise every member variable, whether
  explicitly, through a delegating constructor call, or by default construction.
- (Unknown) If a constructor has an `Ensures` contract, try to check that it
  holds as a postcondition.

## Related

- [0061](0061-c-40-define-a-constructor-if-a-class-has-an-invariant.md) — the
  invariant this constructor establishes.
- [0063](0063-c-42-if-a-constructor-cannot-construct-a-valid-object-throw-an-exception.md)
  — the failure case, and the factory function that handles it.
- [0055](0055-c-31-all-resources-acquired-by-a-class-must-be-released-by-the-classs-destructor.md)
  — the other half of RAII.
- [0026](0026-i-7-state-postconditions.md) — a constructor's postcondition is
  the class's invariant.
