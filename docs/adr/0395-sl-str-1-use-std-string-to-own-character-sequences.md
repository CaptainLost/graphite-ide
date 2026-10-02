# 0395. SL.str.1: Use std::string to own character sequences

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section SL (The Standard Library), transcribed 2026-08-10

## Context

`string` correctly handles allocation, ownership, copying, gradual expansion,
and offers a variety of useful operations.

```cpp
vector<string> read_until(const string& terminator)
{
    vector<string> res;
    for (string s; cin >> s && s != terminator; ) // read a word
        res.push_back(s);
    return res;
}
```

Note how `>>` and `!=` are provided for `string`, as examples of useful
operations, and that there are no explicit allocations, deallocations, or range
checks: `string` takes care of those. Since C++17 the argument can be a
`string_view` rather than a `const string&`, which gives callers more
flexibility:

```cpp
vector<string> read_until(string_view terminator)   // C++17
{
    vector<string> res;
    for (string s; cin >> s && s != terminator; ) // read a word
        res.push_back(s);
    return res;
}
```

C-style strings should not be used for operations that require non-trivial
memory management:

```cpp
char* cat(const char* s1, const char* s2)   // beware!
    // return s1 + '.' + s2
{
    int l1 = strlen(s1);
    int l2 = strlen(s2);
    char* p = (char*) malloc(l1 + l2 + 2);
    strcpy(p, s1, l1);
    p[l1] = '.';
    strcpy(p + l1 + 1, s2, l2);
    p[l1 + l2 + 1] = 0;
    return p;
}
```

Did we get that right? Will the caller remember to `free()` the returned
pointer? Will this code pass a security review? Do not assume that `string` is
slower than lower-level techniques without measurement, and remember that not
all code is performance critical.

## Decision

Where a character sequence is owned, own it with `std::string`.

- Store, return and accumulate text as `std::string`; do not allocate character
  buffers by hand for it.
- Do not return a raw `char*` that the caller is expected to free, and do not
  accept one under that contract.
- Use `string_view` for a parameter that is only read, and `string` for what is
  kept; ownership and reference are separate decisions.
- Use `string`'s own operations — concatenation, comparison, `substr`, the
  stream operators — rather than reproducing them with `strlen`, `strcpy` and
  arithmetic on pointers.

## Rationale

The `cat` example is the argument. It is short, it looks like careful code, and
it is wrong in several independent ways at once — the `strcpy` calls are given
three arguments where the function takes two, the lengths are held in `int`
although `strlen` returns `size_t`, the allocation is never checked for failure,
and the ownership of the result is documented nowhere. Every one of those
defects is a consequence of hand-managing a buffer, and every one of them
disappears when the type owns its storage.

The three questions upstream ends with are rhetorical for a reason: nobody
reviewing that function can answer them by reading it, which is the strongest
possible statement that the abstraction is wrong.

## Consequences

### Positive

- Allocation, growth and release are handled by the type, so text handling stops
  leaking and stops overflowing.
- Ownership of a character sequence becomes visible in the signature.
- The useful operations already exist and compose, so text code shrinks.

### Negative

- `std::string` allocates, and short strings that would have lived in a stack
  buffer now depend on the implementation's small-string optimisation, which is
  not guaranteed.
- Converting at every C API boundary costs a copy in one direction and a
  `.c_str()` in the other, and the copy is easy to do repeatedly by accident.
- `std::string` is a sequence of `char`, not of characters; it does nothing for
  encoding, so it can give a false impression of correctness on non-ASCII text.
- Passing `const string&` where a `string_view` would do quietly forces callers
  holding a `char*` to allocate.

### Neutral

- Non-ASCII character sets and encodings — `wchar_t`, Unicode, UTF-8 — are
  explicitly out of scope for this rule and the section it belongs to.
- Upstream gives no enforcement for this rule.

## Related

- [0396](0396-sl-str-2-use-std-string-view-or-gsl-span-char-to-refer-to-character-sequences.md)
  — the non-owning counterpart; this record covers ownership only.
- [0397](0397-sl-str-3-use-zstring-or-czstring-to-refer-to-a-c-style-zero-terminated-sequence-of-characters.md)
  — what to write when a C-style string genuinely is what is meant.
- [0030](0030-i-11-never-transfer-ownership-by-a-raw-pointer-or-reference.md) —
  why the `char*` returned by `cat` is the wrong interface.
- [0239](0239-per-2-dont-optimize-prematurely.md) — the answer to "but `string`
  is slow", absent a measurement.
