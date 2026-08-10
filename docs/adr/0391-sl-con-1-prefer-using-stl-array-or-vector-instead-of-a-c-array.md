# 0391. SL.con.1: Prefer using STL array or vector instead of a C array

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section SL (The Standard Library), transcribed 2026-08-10

## Context

C arrays are less safe, and have no advantages over `array` and `vector`. For a
fixed-length array, use `std::array`, which does not degenerate to a pointer
when passed to a function and does know its size. Like a built-in array, a
stack-allocated `std::array` keeps its elements on the stack. For a
variable-length array, use `std::vector`, which additionally can change its size
and handles memory allocation.

```cpp
int v[SIZE];                        // BAD
std::array<int, SIZE> w;            // ok
```

```cpp
int* v = new int[initial_size];     // BAD, owning raw pointer
delete[] v;                         // BAD, manual delete
std::vector<int> w(initial_size);   // ok
```

Use `gsl::span` for non-owning references into a container.

Comparing the performance of a fixed-size array allocated on the stack against a
vector with its elements on the free store is bogus. You could just as well
compare a `std::array` on the stack against the result of a `malloc()` accessed
through a pointer. For most code, even the difference between stack allocation
and free-store allocation does not matter, but the convenience and safety of
`vector` does. People working with code for which that difference matters are
quite capable of choosing between `array` and `vector`.

## Decision

Declare `std::array` or `std::vector` where a built-in array would otherwise be
declared.

- Use `std::array<T, N>` for a fixed-length sequence; it stores its elements
  exactly where a built-in array would, and knows its own size.
- Use `std::vector<T>` where the length is decided at run time or changes; it
  owns its allocation and releases it.
- Never write `new T[n]` and a matching `delete[]`; that is a `vector`.
- Pass a `span` where a function needs to see into a sequence it does not own,
  rather than a pointer and a count.
- Do not defend a C array on performance grounds without a measurement that
  distinguishes storage location from container choice.

## Rationale

The decisive property is that `std::array` does not decay. A built-in array
loses its size the moment it is passed anywhere, and everything downstream then
depends on a separately transmitted length that the compiler cannot relate to
the data. `std::array` costs nothing to fix that: same storage, same layout, one
extra piece of information that happens to live in the type.

The performance objection upstream anticipates is almost always a confusion
between `std::array` and `std::vector`. Stack versus free store is a real
difference; container versus built-in array is not.

## Consequences

### Positive

- Sizes travel with the data, so length arguments stop being a separate source
  of truth.
- Array-to-pointer decay, and the derived-to-base conversion it enables, stop
  happening implicitly.
- `vector` removes the paired `new[]`/`delete[]`, and with it a whole class of
  leaks and double frees.
- Both types work directly with range-`for`, algorithms and ranges.

### Negative

- `std::vector` allocates on the free store; replacing a stack array with a
  `vector` rather than an `array` genuinely does change allocation behaviour and
  cost.
- `std::array` normally has to be spelled with both its element type and its
  length, where a built-in array deduces the length from its initialiser; class
  template argument deduction closes the gap only when having the element type
  deduced as well is acceptable.
- Neither type is bounds-checked through `operator[]`, so the safety gained is
  about extent information, not about checked access —
  [0393](0393-sl-con-3-avoid-bounds-errors.md) is a separate rule for a reason.
- Interoperating with C APIs still requires `.data()` and an explicit size, so
  the unsafe shape reappears at every boundary.

### Neutral

- Multidimensional built-in arrays have no equally convenient standard
  replacement; nested `std::array` works but reads worse.
- The rule is about declaring storage; it does not say how a sequence is passed,
  which is
  [0032](0032-i-13-do-not-pass-an-array-as-a-single-pointer.md).

## Enforcement

- Flag the declaration of a C array inside a function or class that also
  declares an STL container, so that legacy non-STL code does not generate
  excessive noise. The minimum fix is to change the C array to a `std::array`.

## Related

- [0032](0032-i-13-do-not-pass-an-array-as-a-single-pointer.md) — the interface
  half of the same problem: decay is what makes a C array dangerous.
- [0190](0190-es-27-use-std-array-or-stack-array-for-arrays-on-the-stack.md) —
  the same preference for stack-allocated sequences.
- [0392](0392-sl-con-2-prefer-using-stl-vector-by-default-unless-you-have-a-reason-to-use-a-different-container.md)
  — which of the two to reach for by default.
- [0154](0154-r-11-avoid-calling-new-and-delete-explicitly.md) — why the
  `new int[n]` example is bad independently of the array question.
