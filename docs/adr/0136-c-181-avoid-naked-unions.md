# 0136. C.181: Avoid "naked" unions

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

A naked union is a union without an associated indicator of which member, if
any, it holds, so that the programmer has to keep track. Naked unions are a
source of type errors.

```cpp
union Value {
    int x;
    double d;
};

Value v;
v.d = 987.654;  // v holds a double
```

So far, so good, but the union is easily misused:

```cpp
cout << v.x << '\n';    // BAD, undefined behavior: v holds a double, but we read it as an int
```

The type error happened without any explicit cast. When upstream tested that
program, the last value printed was 1683627180, which is the integer value for
the bit pattern of 987.654. What we have is an "invisible" type error that
happens to give a result which could easily look innocent. And, talking about
"invisible", this code produced no output at all:

```cpp
v.x = 123;
cout << v.d << '\n';    // BAD: undefined behavior
```

The alternative is to wrap the union in a class together with a type field. The
C++17 `variant` type, found in `<variant>`, does that for you:

```cpp
variant<int, double> v;
v = 123;        // v holds an int
int x = get<int>(v);
v = 123.456;    // v holds a double
double w = get<double>(v);
```

## Decision

Do not use a union that carries no indication of which member is live.

- Use `std::variant` for a set of alternatives, and reach for a raw union only
  where its layout is itself the requirement.
- Where a raw union is used, pair it with a discriminant in the same class — an
  enumeration tag, or an existing field that determines the answer — and make
  that pairing a class invariant.
- Make the union a private member of the wrapping class, and never expose it in
  an interface.
- Route every read through an accessor that checks the discriminant, so that
  reading the wrong member is an error rather than undefined behaviour.
- Do not read a member other than the one last written, whatever the layout
  suggests.

## Rationale

The two failures above differ in kind and both are worse than a crash. One
printed a plausible integer; the other printed nothing. Neither involved a cast,
a warning or any syntax marking the moment where the type was changed — the
error is expressed as an ordinary member access, which is the one construct in
the language a reader will never stop to examine. A discriminant converts that
invisible error into a check the program itself can make, which is the only
class of fix available when the language gives no diagnostic at all.

## Consequences

### Positive

- The type currently held becomes a property of the object rather than knowledge
  held in the programmer's head.
- A wrong read becomes a detectable error at the point it happens, rather than a
  plausible-looking value some distance away.
- `std::variant` supplies the discriminant, the correct special member
  functions, and the checked accessors, none of which need writing again.

### Negative

- The discriminant costs space and a branch on every access, which is a real
  charge against the memory saving that motivated the union in the first place.
- `std::variant` is not layout-compatible with a raw union, so it cannot be used
  where a specific layout is required — a C ABI struct, a file or wire format —
  which is where raw unions genuinely persist.
- A hand-written tagged union is substantially more code than the union it
  replaces, and the tag can still be set wrongly; the rule moves the error, it
  does not eliminate it.
- `std::variant` carries its own awkwardness: a valueless-by-exception state
  that every user must consider, and a `std::get` that reports a wrong-
  alternative access by throwing `std::bad_variant_access` at run time rather
  than by refusing to compile.

### Neutral

- Upstream gives no enforcement for this rule, and detecting a mismatched read
  in general requires tracking which member was last written.
- The rule is about the missing indicator, not about unions as such;
  [0135](0135-c-180-use-unions-to-save-memory.md) endorses the construct.

## Related

- [0135](0135-c-180-use-unions-to-save-memory.md) — the legitimate use, which
  this rule qualifies.
- [0137](0137-c-182-use-anonymous-unions-to-implement-tagged-unions.md) — how to
  build the wrapper when `std::variant` will not do.
- [0138](0138-c-183-dont-use-a-union-for-type-punning.md) — reading a member
  that was not written, done deliberately.
- [0010](0010-p-4-ideally-a-program-should-be-statically-type-safe.md) — a naked
  union is a type error with no cast and no diagnostic.
