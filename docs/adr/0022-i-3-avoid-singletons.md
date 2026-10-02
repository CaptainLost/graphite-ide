# 0022. I.3: Avoid singletons

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section I (Interfaces), transcribed 2026-08-10

## Context

Singletons are basically complicated global objects in disguise:

```cpp
class Singleton {
    // ... lots of stuff to ensure that only one Singleton object is created,
    // that it is initialized properly, etc.
};
```

There are many variants of the singleton idea. That is part of the problem: the
name does not tell a reader what guarantees they are getting.

If a global object should not change, declare it `const` or `constexpr`; that
removes most of the motivation.

There is a narrow exception. The simplest "singleton" — so simple that it is
often not considered one — gives initialisation on first use:

```cpp
X& myX()
{
    static X my_x {3};
    return my_x;
}
```

This is one of the most effective solutions to problems related to
initialisation order. In a multi-threaded environment the initialisation of the
static object does not introduce a race condition, unless a shared object is
carelessly accessed from within its constructor.

The variant that does cause trouble is the one that avoids destruction:

```cpp
X& myX()
{
    static auto p = new X {3};
    return *p;  // potential leak
}
```

Now someone must delete that object in some suitably thread-safe way. That is
error-prone, so the technique is not used unless all three of the following
hold: `myX` is in multi-threaded code; the `X` object needs to be destroyed,
because it releases a resource; and `X`'s destructor's code needs to be
synchronised.

If, as many do, one defines a singleton as a class for which only one object is
created, then functions like `myX` are not singletons at all, and this useful
technique is not an exception to the no-singleton rule.

## Decision

Avoid singletons.

- Do not write a class whose purpose is to guarantee that only one instance of
  it exists, and do not give a class a static accessor returning a
  function-local static of its own type.
- Where a single instance is genuinely wanted, prefer an ordinary object created
  once, owned by whatever owns that part of the program, and passed to what
  needs it.
- Where the object must not change, make it `const` or `constexpr` at namespace
  scope instead.
- Function-local `static` for initialisation on first use is permitted, and is
  the preferred answer to initialisation-order problems. The `new`-without-
  `delete` variant is used only when all three conditions above hold.

## Rationale

A singleton combines the two costs it is meant to solve: it is still a global —
same hidden dependency, same lifetime questions — and it adds machinery, a
variant nobody can name precisely, and a hard-wired assumption that one instance
is enough forever. That last assumption is the one that fails: the second
instance is always wanted eventually, for a test if nothing else, and by then
the type has been called from everywhere.

Passing the object explicitly costs a parameter and buys back testability, an
obvious lifetime, and the ability to have two.

## Consequences

### Positive

- Lifetime and ownership are visible instead of implied by a static accessor.
- Tests can construct their own instance rather than sharing one across the
  whole run.
- A second instance stops being an architectural change.

### Negative

- Threading an object through many layers to reach the code that needs it is
  real friction, and it is exactly the friction singletons exist to avoid.
- The permitted function-local `static` is a hair's breadth from the banned
  pattern; the difference is intent, which no tool can see.
- Destruction order for objects that outlive most of the program remains
  unsolved by this record.

### Neutral

- Whether `myX`-style initialisation-on-first-use counts as a singleton is a
  definitional argument, settled here by permitting it and naming the conditions
  under which the leaking variant is acceptable.
- A global object is often better than a singleton, per
  [0021](0021-i-2-avoid-non-const-global-variables.md), which is faint praise
  for both.

## Enforcement

Very hard in general.

- Look for classes with names that include `singleton`.
- Look for classes for which only a single object is created, by counting
  objects or by examining constructors.
- If a class `X` has a public static function that contains a function-local
  static of the class's type `X` and returns a pointer or reference to it, ban
  that.

## Related

- [0021](0021-i-2-avoid-non-const-global-variables.md) — the plainer form of the
  same problem.
- [0033](0033-i-22-avoid-complex-initialization-of-global-objects.md) — the
  initialisation-order problem the permitted exception solves.
- [0020](0020-i-1-make-interfaces-explicit.md) — a singleton reached from inside
  a function is an implicit interface.
