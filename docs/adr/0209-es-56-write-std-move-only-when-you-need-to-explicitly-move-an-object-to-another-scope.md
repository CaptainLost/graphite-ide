# 0209. ES.56: Write std::move() only when you need to explicitly move an object to another scope

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

We move rather than copy to avoid duplication and for improved performance. A
move typically leaves behind an empty object, which can be surprising or even
dangerous, so we try to avoid moving from lvalues — they might be accessed
later.

Moving is done implicitly when the source is an rvalue, such as a value being
returned or a function result, so there is no need to complicate that code by
writing `move` explicitly. Writing short functions that return values means both
the return and the caller's acceptance of it are optimised naturally. Following
the other guidelines — not making scopes needlessly large, writing short
functions that return values, returning local variables — eliminates most of the
need for explicit `std::move`.

Explicit move is needed to move an object into another scope: notably to pass it
to a sink function, and in the implementations of the move operations themselves
and of swap.

```cpp
void sink(X&& x);   // sink takes ownership of x

void user()
{
    X x;
    // error: cannot bind an lvalue to a rvalue reference
    sink(x);
    // OK: sink takes the contents of x, x must now be assumed to be empty
    sink(std::move(x));
    // ...
    // probably a mistake
    use(x);
}
```

Usually a `std::move()` is used as an argument to a `&&` parameter, and after
that the object must be assumed to have been moved from: do not read its state
again until it has been given a new value.

```cpp
void f()
{
    string s1 = "supercalifragilisticexpialidocious";

    string s2 = s1;             // ok, takes a copy
    assert(s1 == "supercalifragilisticexpialidocious");  // ok

    // bad, if you want to keep using s1's value
    string s3 = move(s1);

    // bad, assert will likely fail, s1 likely changed
    assert(s1 == "supercalifragilisticexpialidocious");
}
```

With a move-only type the language enforces the discipline for you:

```cpp
void sink(unique_ptr<widget> p);  // pass ownership of p to sink()

void f()
{
    auto w = make_unique<widget>();
    // ...
    sink(std::move(w));               // ok, give to sink()
    // ...
    sink(w);    // Error: unique_ptr is carefully designed so that you cannot copy it
}
```

`std::move()` is a cast to `&&` in disguise. It does not itself move anything;
it marks a named object as a candidate to be moved from. The language already
knows the common cases where objects can be moved from, especially when
returning values from functions, so redundant `std::move`s only complicate the
code. Never write `std::move()` because you have heard it is more efficient, and
never write it on a `const` object — it is silently transformed into a copy.

```cpp
vector<int> make_vector()
{
    vector<int> result;
    // ... load result with data
    return std::move(result);       // bad; just write "return result;"
}
```

Never write `return move(local_variable);`. The language already knows the
variable is a move candidate, the `move` does not help, and on some compilers it
is actively harmful because it interferes with the return value optimisation by
creating an additional reference alias to the local.

```cpp
vector<int> v = std::move(make_vector());   // bad; the std::move is entirely redundant
```

Never write `move` on a returned value such as `x = move(f());` where `f`
returns by value; the language already knows a returned value is a temporary
that can be moved from.

```cpp
void mover(X&& x)
{
    call_something(std::move(x));         // ok
    call_something(std::forward<X>(x));   // bad, don't std::forward an rvalue reference
    call_something(x);                    // suspicious, why not std::move?
}

template<class T>
void forwarder(T&& t)
{
    call_something(std::move(t));         // bad, don't std::move a forwarding reference
    call_something(std::forward<T>(t));   // ok
    call_something(t);                    // suspicious, why not std::forward?
}
```

## Decision

Write `std::move` exactly where the object has to leave the current scope, and
nowhere else.

- Write it when passing a named object to a sink — a `&&` parameter, or a
  by-value parameter of a move-only type — and inside move constructors, move
  assignment operators and swap.
- Do not write `return std::move(local)`. Return the local; the language moves
  it, and the explicit move can defeat the return value optimisation.
- Do not apply `std::move` to a temporary, to a function call result, or to
  anything the language already treats as an rvalue.
- Do not apply `std::move` to a `const` object; the result is a silent copy.
- Use `std::forward<T>` on a forwarding reference `T&&` where `T` is a template
  parameter, and `std::move` on a plain rvalue reference. Do not swap them.
- After moving from a named object, do not read it until it has been assigned a
  new value.
- Do not add a `std::move` on the grounds of efficiency without a measurement;
  see
  [0243](0243-per-6-dont-make-claims-about-performance-without-measurements.md).

## Rationale

The strongest argument here is that `std::move` is not an optimisation but a
statement about lifetime: it declares that this scope has finished with the
object. Written where that is not true, it is a lie the compiler will happily
act on, and the reader has no way to distinguish a meaningful move from a
decorative one once the decorative ones are common.

`return std::move(local)` is the case that makes the rule concrete. It is the
most frequently written form of the mistake, it is motivated entirely by a
belief about performance, and it makes the code slower on the compilers where it
suppresses copy elision.

## Consequences

### Positive

- Every remaining `std::move` marks a genuine transfer of ownership, which is
  exactly the set of places a reader must check for use-after-move.
- Return statements stay eligible for copy elision, which is strictly better
  than the move the explicit cast would force.
- The `move`/`forward` distinction is stated once, which is where most template
  forwarding bugs come from.

### Negative

- Applying the rule requires knowing whether an expression is already an rvalue,
  which is one of the less intuitive parts of the language; the rule's cost is
  paid in understanding rather than in typing.
- The boundary is not always clear. Whether a given function is a "sink" is a
  design judgement, and two reviewers can disagree about whether a particular
  `std::move` is required.
- `std::move` on a `const` object compiles, does the wrong thing, and produces
  no diagnostic — the rule names the trap but the language does not help.
- The rule asks for a moved-from object not to be read, but the language permits
  it and most standard types leave a valid state behind, so violations run
  correctly for a long time before a type with a different moved-from state
  appears.

### Neutral

- Whether the return value optimisation would have applied is
  implementation-specific; the guidance not to write `return std::move(x)` is
  correct in either case, since the implicit move applies anyway.
- Nothing here says how often to move rather than copy; it governs only when the
  explicit cast is written.

## Enforcement

- Flag `std::move(x)` where `x` is an rvalue or where the language already
  treats it as one, including `return std::move(local_variable);` and
  `std::move(f())` for an `f` that returns by value.
- Flag functions taking an `S&&` parameter where there is no `const S&` overload
  to take care of lvalues.
- Flag a `std::move`d argument passed to a parameter, except where the parameter
  is an `X&&` rvalue reference or the type is move-only and the parameter is
  passed by value.
- Flag `std::move` applied to a forwarding reference — `T&&` where `T` is a
  template parameter type; `std::forward` belongs there.
- Flag `std::move` applied to anything other than an rvalue reference to
  non-const.
- Flag `std::forward` applied to an rvalue reference `X&&` where `X` is not a
  template parameter type; `std::move` belongs there.
- Flag `std::forward` applied to anything other than a forwarding reference.
- Flag an object that is potentially moved from where the next operation is a
  const operation; there should first be an intervening non-const operation,
  ideally an assignment, to reset the object's value.

## Related

- [0078](0078-c-64-a-move-operation-should-move-and-leave-its-source-in-a-valid-state.md)
  — what the moved-from object is guaranteed to be.
- [0206](0206-es-49-if-you-must-use-a-cast-use-a-named-cast.md) — `std::move` is
  one of the named casts.
- [0243](0243-per-6-dont-make-claims-about-performance-without-measurements.md) —
  the belief that motivates most redundant moves.
- [0175](0175-es-5-keep-scopes-small.md) — small scopes and short functions are
  what remove the need for explicit moves in the first place.
