# 0135. C.180: Use unions to save memory

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

A union is a struct where all members start at the same address, so that it can
hold only one member at a time. It does not keep track of which member is
stored, so the programmer has to get that right; this is inherently error-prone,
but there are ways to compensate. A type that is a union plus an indicator of
which member is currently held is called a tagged union, a discriminated union,
or a variant.

A union allows a single piece of memory to be used for different types of
objects at different times. Consequently, it can be used to save memory when
there are several objects that are never used at the same time.

```cpp
union Value {
    int x;
    double d;
};

Value v = { 123 };  // now v holds an int
cout << v.x << '\n';    // write 123
v.d = 987.654;  // now v holds a double
cout << v.d << '\n';    // write 987.654
```

But heed the warning against naked unions. The realistic use is one where the
saving is structural, such as the short-string optimisation:

```cpp
// Short-string optimization
constexpr size_t buffer_size = 16; // Slightly larger than the size of a pointer

class Immutable_string {
public:
    Immutable_string(const char* str) :
        size(strlen(str))
    {
        if (size < buffer_size)
            strcpy_s(string_buffer, buffer_size, str);
        else {
            string_ptr = new char[size + 1];
            strcpy_s(string_ptr, size + 1, str);
        }
    }

    ~Immutable_string()
    {
        if (size >= buffer_size)
            delete[] string_ptr;
    }

    const char* get_str() const
    {
        return (size < buffer_size) ? string_buffer : string_ptr;
    }

private:
    // If the string is short enough, we store the string itself
    // instead of a pointer to the string.
    union {
        char* string_ptr;
        char string_buffer[buffer_size];
    };
    const size_t size;
};
```

Note that the discriminant here is not a separate tag: `size` already
determines which member is live, which is what makes the class safe to use.

## Decision

Reach for a union when, and only when, the purpose is to reuse storage for
alternatives that are never live at once.

- Establish first that the alternatives really are mutually exclusive over the
  object's lifetime; if two can be needed simultaneously, a union is wrong at
  the outset.
- Establish second that the saving matters — that the type is instantiated
  enough times, or is small enough relative to its members, for the difference
  to be real.
- Pair the union with something that determines which member is live, whether an
  explicit tag or an existing field such as a size, and keep that determination
  a class invariant.
- Wrap the union inside a class that owns the invariant, and never expose it as
  a member of a public interface.
- Prefer `std::variant`, which does all of the above correctly, unless the
  layout control is itself the requirement.
- Do not use a union to reinterpret one type's bytes as another; that is
  [0138](0138-c-183-dont-use-a-union-for-type-punning.md).

## Rationale

The union is the only construct in C++ that lets two members share an address,
so where mutually exclusive alternatives must occupy one object it is not merely
the cheapest answer but the only one — and the short-string optimisation, which
is why `std::string` is fast for the strings programs actually use, exists
because of it. The reason to say so explicitly is that the rule immediately
following this one is a warning, and the pair should not be read as a
prohibition: the union is endorsed for this purpose and for this purpose only.

## Consequences

### Positive

- Mutually exclusive alternatives cost the size of the largest rather than the
  sum, which for a widely instantiated type is a structural saving.
- Keeping the alternatives inline avoids an allocation and an indirection that a
  pointer-based design would need.
- Stating the legitimate use makes the illegitimate ones easier to identify.

### Negative

- The union supplies no safety of its own: every guarantee comes from the
  invariant the surrounding class maintains, which is hand-written and
  unverified.
- A union with non-trivial members has its special member functions implicitly
  deleted, so the wrapping class must write construction, copying, assignment
  and destruction by hand, as
  [0137](0137-c-182-use-anonymous-unions-to-implement-tagged-unions.md) shows.
- The saving is easy to assume and hard to confirm; padding and alignment can
  consume it, and a `variant`'s discriminant costs a word anyway.
- The example's own class declares a destructor but no copy operations, so
  copying a long `Immutable_string` copies the pointer and both objects
  `delete[]` it — which is exactly how quickly this construct goes wrong.

### Neutral

- Upstream offers no enforcement for this rule; whether the memory saving was
  warranted is a design judgement.
- `std::variant` is implemented over the same mechanism, so choosing it is a
  choice about who writes the invariant, not about avoiding unions.

## Related

- [0136](0136-c-181-avoid-naked-unions.md) — the warning this rule defers to.
- [0137](0137-c-182-use-anonymous-unions-to-implement-tagged-unions.md) — how
  the union is wrapped so the invariant is maintained.
- [0138](0138-c-183-dont-use-a-union-for-type-punning.md) — the use this rule
  does not endorse.
- [0015](0015-p-9-do-not-waste-time-or-space.md) — saving space is the entire
  justification, and it should be measured.
