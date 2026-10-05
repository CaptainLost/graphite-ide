# 0423. NL.5: Avoid encoding type information in names

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section NL (Naming and layout suggestions), transcribed 2026-08-10

## Context

If names reflect types rather than functionality, it becomes hard to change the
types used to provide that functionality. If the type of a variable is changed,
code using it has to be modified as well. Encoding the type in the name also
multiplies the opportunities for an unintentional conversion.

Upstream's bad example encodes the parameter type in the function name:

```cpp
void print_int(int i);
void print_string(const char*);
print_int(1);          // repetitive, manual type matching
print_string("xyzzy"); // repetitive, manual type matching
```

The good one lets overload resolution do the matching:

```cpp
void print(int i);
void print(string_view);    // also works on any string-like sequence
print(1);              // clear, automatic type matching
print("xyzzy");        // clear, automatic type matching
```

Names with types encoded are either verbose or cryptic:

```cpp
printS  // print a std::string
prints  // print a C-style string
printi  // print an int
```

Techniques such as Hungarian notation, which encode a type in the name, were
used in untyped languages. In a strongly statically typed language like C++
they are generally unnecessary and actively harmful, because the annotations go
out of date — the warts are just like comments and rot just like them — and
because they interfere with good use of the language: the right tool is the same
name plus overload resolution.

Upstream is careful to say what the rule does *not* cover, and it excludes three
common conventions explicitly. A general prefix that says how a name is used
rather than what type it has is fine:

```cpp
auto p = new User();
auto p = make_unique<User>();
// note: "p" is not being used to say "raw pointer to type User,"
//       just generally to say "this is an indirection"
auto cntHits = calc_total_of_hits(/*...*/);
// note: "cnt" is not being used to encode a type,
//       just generally to say "this is a count of something"
```

Distinguishing members from locals or globals is fine:

```cpp
struct S {
    int m_;
    S(int m) : m_{abs(m)} { }
};
```

And distinguishing types from non-types, for example by capitalising type names
but not functions and variables, is fine:

```cpp
typename<typename T>
class HashTable {   // maps string to T
    // ...
};
HashTable<int> index;
```

None of these encode type information, so none of them fall under this rule.

## Decision

Name things for what they are for, not for what type they are.

- Do not put a type name, an abbreviation of one, or a Hungarian-style prefix
  into an identifier to say what its type is.
- Give the same operation on different types the same name and let overload
  resolution pick, instead of `print_int` and `print_string`.
- Prefer a parameter type that generalises — `string_view` over `const char*` —
  rather than one name per concrete representation.
- Keep the conventions the rule excludes: a prefix or suffix marking a data
  member, capitalised type names, and short role prefixes such as `p` or `cnt`
  that say how a name is used rather than what type it holds.

## Rationale

The sharpest argument is that a type-encoded name is a comment fixed to the
identifier, and it rots the same way, except that it also has to be edited at
every use site when the type changes. That converts a one-line change into a
sweep, and a sweep is precisely where a wrong name gets left behind — after
which the name is actively misleading rather than merely redundant.

## Consequences

### Positive

- Changing a type does not require renaming its variables or its callers.
- Call sites read as operations rather than as manual type dispatch.
- Generalising a parameter type later is a compatible change, because no name
  promised the old one.

### Negative

- Overloading is not free: an overload set can be selected in ways the author
  did not intend, and the diagnostics when it goes wrong are far worse than
  those for a misspelled `print_int`.
- In code with no IDE and no hover — a diff in a terminal, a review in a browser
  — the encoded type was doing real work, and removing it costs the reader a
  lookup.
- The line between "encodes a type" and "says how it is used" is genuinely
  blurry. `cnt` is allowed, `pStr` is not, and a convention will accumulate
  cases near the boundary that nobody can settle.

### Neutral

- The rule is about names, not about type safety; a badly named variable of a
  strong type is still type-safe.
- Member prefixes and capitalised type names are placed outside the rule
  explicitly, so a convention using them is not in tension with it.

## Related

- [0425](0425-nl-8-use-a-consistent-naming-style.md) — a house style fixes which
  of the permitted conventions are used.
- [0177](0177-es-7-keep-common-and-local-names-short-and-keep-uncommon-and-non-local-names-longer.md)
  — the other axis of naming: length against scope.
- [0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md) — the type
  system carries this information properly, which is why the name need not.
- [0009](0009-p-3-express-intent.md) — a name should express purpose, and a type
  wart expresses representation.
- [0559](0559-name-every-variable-for-its-role-loop-indices-included.md) — amends this record: every name states its role, loop indices included.
