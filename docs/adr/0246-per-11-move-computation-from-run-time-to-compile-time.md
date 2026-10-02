# 0246. Per.11: Move computation from run time to compile time

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section Per (Performance), transcribed 2026-08-10

## Context

Moving computation to compile time decreases code size and run time, avoids data
races by producing constants, and catches errors at compile time — which in turn
removes the need for error-handling code that would have dealt with them at run
time.

```cpp
double square(double d) { return d*d; }
static double s2 = square(2);    // old-style: dynamic initialization

constexpr double ntimes(double d, int n)   // assume 0 <= n
{
        double m = 1;
        while (n--) m *= d;
        return m;
}
constexpr double s3 {ntimes(2, 3)};  // modern-style: compile-time initialization
```

Code like the initialisation of `s2` is not uncommon, particularly where the
initialisation is a little more complicated than `square()`. Compared with `s3`
it has two problems: the overhead of a function call at run time, and the
possibility that `s2` is accessed by another thread before the initialisation
has happened. There can be no data race on a constant.

The second example is a popular technique for a handle that stores small objects
in the handle itself and larger ones on the heap:

```cpp
constexpr int on_stack_max = 20;

template<typename T>
struct Scoped {     // store a T in Scoped
        // ...
    T obj;
};

template<typename T>
struct On_heap {    // store a T on the free store
        // ...
        T* objp;
};

template<typename T>
using Handle = typename std::conditional<(sizeof(T) <= on_stack_max),
                    Scoped<T>,      // first alternative
                    On_heap<T>      // second alternative
               >::type;

void f()
{
    Handle<double> v1;                   // the double goes on the stack
    Handle<std::array<double, 200>> v2;  // the array goes on the free store
    // ...
}
```

Assuming `Scoped` and `On_heap` present compatible interfaces, the optimal type
is computed at compile time. Similar techniques exist for selecting the optimal
function to call.

The ideal is not to execute everything at compile time. Most computations depend
on inputs and so cannot be moved, and beyond that logical limit there are
practical ones: complex compile-time computation can seriously increase compile
times and complicate debugging. It is even possible to slow code down by
computing at compile time — rare, but factoring a general computation into
separate optimal sub-calculations can render the instruction cache less
effective.

## Decision

Compute at compile time what does not depend on run-time input.

- Mark a function `constexpr` when its body permits it and it is called with
  constant arguments; each standard since C++11 has widened what a `constexpr`
  function may contain, and C++20 admits far more than earlier ones did.
- Initialise namespace-scope and `static` objects with constant expressions.
  Prefer `constexpr` — or `constinit` where the object must be mutable — over a
  dynamic initialiser, which costs a run-time call and admits a race on first
  access.
- Use `consteval` where a computation must never fall back to run time.
- Replace object-like and simple function-like macros with `constexpr`
  variables and functions, which are typed and scoped.
- Select types and overloads at compile time — `std::conditional`, `if
  constexpr`, concepts — where the choice is determined by a static property such
  as `sizeof`.
- Stop where the cost turns: do not push a computation into the type system when
  it depends on input, when it makes the code unreadable, or when it inflates
  build times out of proportion to the run-time saving.

## Rationale

The strongest part of this rule is not the saved cycles but the eliminated
categories. A value computed at compile time cannot be involved in a data race,
cannot fail at run time, and needs no error path — three whole kinds of code
that simply do not have to exist.

The `s2` example shows how ordinary the loss is. Nothing about it looks like a
performance defect; it is a plain initialiser that happens to run at start-up
and happens to be racy, and `constexpr` removes both properties without changing
what the code says.

## Consequences

### Positive

- Run-time work and code size both go down, and the constant may be folded
  directly into its uses.
- Constants cannot be raced on, which removes an initialisation-order and
  thread-safety concern rather than mitigating it.
- Errors in a compile-time computation are diagnosed at compile time, so no
  run-time error handling is needed for them.

### Negative

- Compile times rise, sometimes steeply. In a codebase already using templates
  heavily, this is a cost paid by every developer on every build, in exchange
  for run-time savings that may be small.
- Compile-time code is harder to debug: no debugger steps through it, and the
  diagnostics for a failed `constexpr` evaluation are notoriously poor.
- It can be slower. Upstream says so directly: expanding a general computation
  into specialised sub-calculations can hurt the instruction cache.
- `constexpr` on a function is part of its interface. Removing it later breaks
  callers that used it in constant expressions, so it is a commitment, not an
  annotation.

### Neutral

- Most computation depends on input and is not a candidate; this rule concerns
  the part that does not.
- Marking a function `constexpr` does not force compile-time evaluation. It
  permits it, and the compiler decides, unless `consteval` is used.

## Enforcement

- Look for simple functions that could be `constexpr` but are not.
- Look for functions called with all constant-expression arguments.
- Look for macros that could be `constexpr`.

## Related

- [0011](0011-p-5-prefer-compile-time-checking-to-run-time-checking.md) — the
  same move applied to checking rather than computation.
- [0016](0016-p-10-prefer-immutable-data-to-mutable-data.md) — a compile-time
  constant is the strongest form of immutable data.
- [0245](0245-per-10-rely-on-the-static-type-system.md) — selecting a type by
  `sizeof` is this rule and that one together.
- [0015](0015-p-9-do-not-waste-time-or-space.md) — a run-time call to compute a
  constant is waste in its plainest form.
