# 0416. NR.5: Don't use two-phase initialization

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section NR (Non-Rules and myths), transcribed 2026-08-10

## Context

The NR section collects rules that are popular somewhere but that the
guidelines deliberately do not recommend. This record adopts the rejection, not
the practice being rejected.

The practice here is two-phase initialization: a constructor that only records
its arguments, followed by a separate `Init()` that does the real work and
reports success. Splitting initialization in two leads to weaker invariants, to
more complicated code — every operation has to cope with a semi-constructed
object — and to errors, when the semi-constructed cases are not handled
consistently.

```cpp
// Old conventional style: many problems
class Picture
{
    int mx;
    int my;
    int * data;
public:
    // main problem: constructor does not fully construct
    Picture(int x, int y)
    {
        mx = x;         // also bad: assignment in constructor body
                        // rather than in member initializer
        my = y;
        data = nullptr; // also bad: constant initialization in constructor
                        // rather than in member initializer
    }
    ~Picture()
    {
        Cleanup();
    }
    // ...
    // bad: two-phase initialization
    bool Init()
    {
        // invariant checks
        if (mx <= 0 || my <= 0) {
            return false;
        }
        if (data) {
            return false;
        }
        data = (int*) malloc(mx*my*sizeof(int));   // also bad: owning raw * and malloc
        return data != nullptr;
    }
    // also bad: no reason to make cleanup a separate function
    void Cleanup()
    {
        if (data) free(data);
        data = nullptr;
    }
};
Picture picture(100, 0); // not ready-to-use picture here
// this will fail..
if (!picture.Init()) {
    puts("Error, invalid picture");
}
// now have a invalid picture object instance.
```

The last line is the whole objection: after the failure has been reported and
handled, the object still exists, still has a destructor, and is still in scope
to be used. Establishing the invariant in the constructor removes that state
entirely:

```cpp
class Picture
{
    int mx;
    int my;
    vector<int> data;
    static int check_size(int size)
    {
        // invariant check
        Expects(size > 0);
        return size;
    }
public:
    // even better would be a class for a 2D Size as one single parameter
    Picture(int x, int y)
        : mx(check_size(x))
        , my(check_size(y))
        // now we know x and y have a valid size
        , data(mx * my) // will throw std::bad_alloc on error
    {
        // picture is ready-to-use
    }
    // compiler generated dtor does the job.
    // ...
};
Picture picture1(100, 100);
// picture is ready-to-use here...
// not a valid size for y,
// default contract violation behavior will call std::terminate then
Picture picture2(100, 0);
// not reach here...
```

The good version also drops the separate `Cleanup()`, because a `vector` member
makes the compiler-generated destructor sufficient.

## Decision

Do not split construction into a constructor plus an `Init()`, and do not accept
a type that requires one.

- A constructor establishes the class invariant, or the object does not come
  into existence. There is no state in which an object of the type exists but is
  not usable.
- Validate arguments in a member initializer — through a small static checking
  helper, as `check_size` does — rather than in the constructor body, so that
  members are initialized rather than assigned.
- Prefer members that manage their own storage, so that no explicit cleanup
  function is needed and the generated destructor suffices.
- Do not add a `Cleanup()` that the destructor calls and that callers may also
  call; releasing resources is the destructor's job alone.
- Where construction can fail for reasons the environment produces, report the
  failure at the point of construction — by throwing from the constructor, or
  by a static factory that either yields a fully constructed object or yields
  none. What must not exist is a returned object that is not yet usable.

## Rationale

Two-phase initialization does not remove the failure, it moves the failure
inside the type's set of valid states. Once a half-built `Picture` can exist,
every member function acquires an implicit precondition that `Init()` was called
and succeeded, and none of them can be checked by the compiler; the invariant
that would have made the class reasonable no longer holds for all its instances.

The alternative costs nothing, because the language already provides the
mechanism: member initializers run before the body, so a check placed there
prevents the object from existing at all rather than reporting on one that
already does.

## Consequences

### Positive

- Every object of the type satisfies its invariant, so member functions need no
  "is it initialized?" precondition.
- The destructor becomes symmetric with the constructor, which is what makes
  RAII and exception safety work.
- The failure is reported where the object would have been created, so it cannot
  be checked late or not at all.

### Negative

- The rule assumes a way for a constructor to fail. Upstream's is a thrown
  exception; where exceptions are unavailable, a fallible type needs a static
  factory function instead, which is a shape the language fights and which
  forfeits aggregate and in-place construction.
- Objects that genuinely acquire their resources later — a window bound to a
  platform handle, a connection opened on demand — must either be constructed
  late or be modelled as two types, and that is more design work than an
  `Init()` call.
- Argument checking in a member initializer is awkward to write: it needs a
  static helper per check, and the order of member declaration then determines
  the order of validation.
- Upstream's good example reacts to a contract violation by terminating, which
  is a heavy default and is not obviously the right response for every caller.

### Neutral

- A static factory function that returns either a fully constructed object or a
  failure is not two-phase initialization: no half-built object is ever handed
  out, so the rule is satisfied by a different mechanism.
- The rule says nothing about deferring expensive work. A lazily computed cache
  behind a valid invariant is not two-phase initialization.

## Related

- [0061](0061-c-40-define-a-constructor-if-a-class-has-an-invariant.md) — the
  alternative upstream offers: establish the invariant in a constructor.
- [0062](0062-c-41-a-constructor-should-create-a-fully-initialized-object.md) —
  the same requirement stated positively.
- [0070](0070-c-49-prefer-initialization-to-assignment-in-constructors.md) — why
  the bad example's assignments in the body are also wrong.
