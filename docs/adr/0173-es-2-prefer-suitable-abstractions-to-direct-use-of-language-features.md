# 0173. ES.2: Prefer suitable abstractions to direct use of language features

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

A suitable abstraction — a library or a class — is closer to the application's
concepts than the bare language is. It leads to shorter and clearer code, and it
is likely to be better tested than a fresh assembly of raw language features.

```cpp
vector<string> read1(istream& is)   // good
{
    vector<string> res;
    for (string s; is >> s;)
        res.push_back(s);
    return res;
}
```

The more traditional and lower-level near-equivalent is longer, messier, harder
to get right, and most likely slower:

```cpp
char** read2(istream& is, int maxelem, int maxstring, int* nread)   // bad: verbose and incomplete
{
    auto res = new char*[maxelem];
    int elemcount = 0;
    while (is && elemcount < maxelem) {
        auto s = new char[maxstring];
        is.read(s, maxstring);
        res[elemcount++] = s;
    }
    nread = &elemcount;
    return res;
}
```

Once checking for overflow and error handling have been added, that code gets
quite messy — and there remains the problem of remembering to delete the
returned pointer and the C-style strings the array contains. Note also that
`read2` takes four parameters where `read1` takes one, and that two of them
exist only because a raw array cannot describe its own extent.

## Decision

Work in terms of types that name application concepts, not in terms of the raw
language constructs those types are built from.

- Return and pass owning containers and vocabulary types (`std::vector`,
  `std::string`, `std::optional`) rather than pointer-plus-count parameter sets.
- Let a type's destructor manage its resource instead of pairing an allocation
  with a matching release by hand.
- When a concept recurs — a file path, a buffer, an offset into a document —
  give it a type rather than passing the primitive it happens to be represented
  by.
- Introduce a small class when a group of raw operations always occurs together;
  the abstraction is cheap and the invariant it holds is not expressible
  otherwise.

## Rationale

The bad example is not merely uglier: it is *incomplete*, and it is incomplete
in a way that its own shape hides. It has no way to report that the input
exceeded `maxelem`, it leaks on every early exit, and its last statement assigns
to the parameter `nread` itself — a local copy of the pointer — instead of
writing `*nread`, so the count never reaches the caller and the address of a
dead local is what gets stored. That is a bug the reader has to find. `read1`
has none of these failure modes available to it because
`std::vector` and `std::string` have already absorbed the decisions that could
go wrong.

## Consequences

### Positive

- Fewer parameters, fewer manual sizes, and fewer states in which the code can
  be half-correct.
- Ownership and lifetime become properties of the type rather than obligations
  on the caller.
- Code reads at the level of the problem, which is what makes it reviewable.

### Negative

- Abstractions are not free: an intermediate container may allocate where a
  raw loop over a caller-supplied buffer would not.
- A badly chosen abstraction is worse than none, because it must be learned,
  maintained, and eventually unwound; "suitable" is doing a lot of work in this
  rule and it is not defined.
- Debugging through several layers of templates is slower than debugging a flat
  loop, and stepping into standard library internals is rarely useful.
- Interop with C APIs and with third-party libraries still requires the raw form at
  the boundary, so both styles coexist.

### Neutral

- This rule does not say which abstraction; it says that some abstraction beats
  none. Choosing among them is a separate decision.
- Writing the abstraction yourself is permitted when nothing suitable exists,
  and is still preferable to spreading the raw construct through the code.

## Enforcement

Not easy to check mechanically. Look for messy loops, nested loops, long
functions, an absence of function calls, and a lack of use of non-built-in
types. Cyclomatic complexity is a usable proxy.

## Related

- [0172](0172-es-1-prefer-the-standard-library-to-other-libraries-and-to-handcrafted-code.md)
  — the same argument, narrowed to the standard library.
- [0007](0007-p-1-express-ideas-directly-in-code.md) — an abstraction is how an
  idea gets expressed directly.
- [0017](0017-p-11-encapsulate-messy-constructs-rather-than-spreading-through-the-code.md)
  — where no suitable abstraction exists, write one and confine the mess to it.
- [0187](0187-es-24-use-a-unique-ptr-t-to-hold-pointers.md) — the specific
  abstraction that removes the leak in `read2`.
