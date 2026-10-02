# 0199. ES.42: Keep use of pointers simple and straightforward

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Complicated pointer manipulation is a major source of errors. A pointer should
refer to a single object. Pointer arithmetic is fragile and easy to get wrong,
and it is the source of many bad bugs and security violations. A `span` is a
bounds-checked, safe type for accessing arrays of data; access into an array
with known bounds using a constant subscript can be validated by the compiler.

```cpp
void f(int* p, int count)
{
    if (count < 2) return;

    int* q = p + 1;    // BAD

    ptrdiff_t d;
    int n;
    d = (p - &n);      // OK
    d = (q - p);       // OK

    int n = *p++;      // BAD

    if (count < 6) return;

    p[4] = 1;          // BAD
    p[count - 1] = 2;  // BAD

    use(&p[0], 3);     // BAD
}
```

The same function over a span says the same thing without any of it being
unchecked:

```cpp
void f(span<int> a) // BETTER: use span in the function declaration
{
    if (a.size() < 2) return;

    int n = a[0];      // OK

    span<int> q = a.subspan(1); // OK

    if (a.size() < 6) return;

    a[4] = 1;          // OK
    a[a.size() - 1] = 2;  // OK

    use(a.data(), 3);  // OK
}
```

Subscripting with a variable is difficult for both tools and humans to validate
as safe. A span is run-time bounds-checked; `at()` is another alternative that
ensures single accesses are bounds-checked. If iterators are needed to access an
array, use the iterators from a span constructed over the array.

```cpp
void f(array<int, 10> a, int pos)
{
    a[pos / 2] = 1; // BAD
    a[pos - 1] = 2; // BAD
    a[-1] = 3;    // BAD (but easily caught by tools) -- no replacement, just don't do this
    a[10] = 4;    // BAD (but easily caught by tools) -- no replacement, just don't do this
}
```

There are three ways out. Change the parameter type, introduce a local span, or
use a checked accessor:

```cpp
void f1(span<int, 10> a, int pos) // A1: Change parameter type to use span
{
    a[pos / 2] = 1; // OK
    a[pos - 1] = 2; // OK
}

void f2(array<int, 10> arr, int pos) // A2: Add local span and use that
{
    span<int> a = {arr.data(), pos};
    a[pos / 2] = 1; // OK
    a[pos - 1] = 2; // OK
}

void f3(array<int, 10> a, int pos) // ALTERNATIVE B: Use at() for access
{
    at(a, pos / 2) = 1; // OK
    at(a, pos - 1) = 2; // OK
}
```

A hand-written index loop over a raw array is the same problem in miniature:

```cpp
void f()
{
    int arr[COUNT];
    for (int i = 0; i < COUNT; ++i)
        arr[i] = i; // BAD, cannot use non-constant indexer
}
```

and it has the same three fixes, of which the range-for is the shortest:

```cpp
void f1()
{
    int arr[COUNT];
    span<int> av = arr;
    for (int i = 0; i < COUNT; ++i)
        av[i] = i;
}

void f3()
{
    int arr[COUNT];
    int i = 0;
    for (auto& e : arr)
         e = i++;
}
```

Turning an array into a pointer, as the language does essentially always,
removes the opportunity for any of this checking, so avoid it:

```cpp
void g(int* p);

void f()
{
    int a[5];
    g(a);        // BAD: are we trying to pass an array?
    g(&a[0]);    // OK: passing one object
}
```

If you want to pass an array, say so:

```cpp
void g(int* p, size_t length);  // old (dangerous) code
void g1(span<int> av); // BETTER: get g() changed.

void f2()
{
    int a[5];
    span<int> av = a;

    g(av.data(), av.size());   // OK, if you have no choice
    g1(a);                     // OK -- no decay here, instead use implicit span ctor
}
```

## Decision

Keep every pointer a pointer to one object, and put anything with an extent
behind a range type.

- Do not perform arithmetic on a pointer: no `p + 1`, no `*p++`, no `p[n]` for a
  raw pointer parameter. Pointer subtraction to compute a distance within one
  object is fine.
- Take a `std::span` where a sequence is meant, and let the span's extent drive
  the bounds; see also
  [0032](0032-i-13-do-not-pass-an-array-as-a-single-pointer.md).
- Where a sequence must be indexed with a computed value, index a span rather
  than an array or a pointer, or construct a local span over the array first.
- Do not let an array decay to a pointer at a call site. If a call really does
  mean "one object", write `&a[0]` so it says so.
- Where an existing `(pointer, length)` interface cannot be changed, build the
  span first and pass `av.data(), av.size()` from it, so that the length is
  derived rather than repeated.
- Prefer a range-for over an index loop whenever the index is not itself needed;
  see [0217](0217-es-71-prefer-a-range-for-statement-to-a-for-statement-when-there-is-a-choice.md).

## Rationale

The value of this rule is that it converts a class of bug that is invisible into
one that is a type error. `p[count - 1]` and `a[a.size() - 1]` compile to
approximately the same instructions, but only one of them carries the bound
alongside the data, so only one of them can be checked at all — by a compiler, a
sanitiser, or a human reading the line in isolation.

The decay example is the part that is easy to underestimate. `g(a)` compiling
silently is what makes every other pointer rule necessary: once the extent has
been thrown away at the call site, no discipline inside the callee can recover
it.

## Consequences

### Positive

- Bounds travel with the data, so a subscript can be checked rather than
  reasoned about.
- Array-to-pointer decay stops happening at call sites, which removes the whole
  category of "is this one object or many?" ambiguity.
- Code written against spans is shorter than code written against a pointer and
  a separate count, and the count cannot fall out of step.

### Negative

- `std::span` is available in C++20, but `std::span::operator[]` is not
  required to be bounds-checked. The safety on offer is the ability to check,
  not a guarantee of checking; whether it is checked depends on the standard
  library implementation and the build configuration.
- The `at()` alternative the rule leans on has no standard equivalent. Without
  the guidelines support library, the choice narrows to spans plus whatever
  hardening the toolchain provides.
- A span is a non-owning view. Replacing a pointer with a span removes the
  bounds problem and leaves the lifetime problem exactly where it was, while
  making the code look safer than it is.
- C APIs take pointers and lengths, so the boundary code still contains
  everything this rule forbids; the rule relocates raw pointer handling into
  wrappers rather than eliminating it.
- `span<int> a = {arr.data(), pos}` in the upstream fix is itself an unchecked
  construction: nothing verifies that `pos` is within `arr`.

### Neutral

- Pointer subtraction within a single object remains well defined and is not
  affected by this record.
- How a sequence is stored is not this rule's concern; it governs how a sequence
  is passed and accessed.

## Enforcement

- Flag any arithmetic operation on an expression of pointer type that results in
  a value of pointer type.
- Flag any indexing expression on an expression or variable of array type,
  static array or `std::array`, where the index is not a compile-time constant
  expression with a value between zero and the upper bound of the array.
- Flag any expression that would rely on implicit conversion of an array type to
  a pointer type.

This rule is part of the bounds-safety profile.

## Related

- [0032](0032-i-13-do-not-pass-an-array-as-a-single-pointer.md) — the interface
  half of the same argument.
- [0157](0157-r-14-avoid-parameters-prefer-span.md) — array parameters decay;
  take a span instead.
- [0149](0149-r-3-a-raw-pointer-a-t-is-non-owning.md) — what a raw pointer is
  allowed to mean once it is kept simple.
- [0217](0217-es-71-prefer-a-range-for-statement-to-a-for-statement-when-there-is-a-choice.md)
  — the loop form that removes the subscript entirely.
