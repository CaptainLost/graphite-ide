# 0177. ES.7: Keep common and local names short, and keep uncommon and non-local names longer

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Name length should track how far a name travels and how conventional it is. The
purpose is readability, and — for non-local names — lowering the chance of
clashes between unrelated entities.

Conventional short local names increase readability:

```cpp
template<typename T>    // good
void print(ostream& os, const vector<T>& v)
{
    for (gsl::index i = 0; i < v.size(); ++i)
        os << v[i] << '\n';
}
```

An index is conventionally called `i`, and in a generic function there is no
hint available about the meaning of the vector, so `v` is as good a name as any.
Compare:

```cpp
template<typename Element_type>   // bad: verbose, hard to read
void print(ostream& target_stream, const vector<Element_type>& current_vector)
{
    for (gsl::index current_element_index = 0;
         current_element_index < current_vector.size();
         ++current_element_index
    )
    target_stream << current_vector[current_element_index] << '\n';
}
```

It is a caricature, but worse has been seen in real code. The long names add no
information, and the loop no longer fits on one line, so its shape has to be
reconstructed by the reader.

The rule runs the other way for non-local names. Unconventional short non-local
names obscure code:

```cpp
void use1(const string& s)
{
    // ...
    tt(s);   // bad: what is tt()?
    // ...
}
```

Better to give non-local entities readable names:

```cpp
void use1(const string& s)
{
    // ...
    trim_tail(s);   // better
    // ...
}
```

There is at least a chance that the reader knows what `trim_tail` means, and
that they will remember it after looking it up once.

Argument names of large functions are de facto non-local, and should be
meaningful:

```cpp
void complicated_algorithm(vector<Record>& vr, const vector<int>& vi, map<string, int>& out)
// read from events in vr (marking used Records) for the indices in
// vi placing (name, index) pairs into out
{
    // ... 500 lines of code using vr, vi, and out ...
}
```

Functions should be kept short, but that rule is not universally adhered to, and
naming should reflect the reality: a name used five hundred lines from its
declaration is not a local name however it was declared.

## Decision

Choose name length in proportion to the distance between the declaration and the
uses.

- Use short conventional names — `i`, `j`, `n`, `p`, `s`, `v` — for variables
  whose whole life is a few lines and whose role is the conventional one.
- Use descriptive names for anything visible outside a function: types,
  functions, members, namespaces, and file-scope constants.
- Treat the parameters of a long function as non-local, and name them
  accordingly, regardless of how they are declared.
- Do not pad a local name with its type, its container, or the word `current`;
  that information is already in the declaration one line up.
- Do not abbreviate a non-local name to initials. `tt` is not a name.

## Rationale

The two halves of this rule look contradictory and are not: both say that a name
should carry exactly the information the reader lacks at the point of use. A
reader looking at `i` inside a three-line loop can see the declaration, so the
name has nothing left to tell them; a reader looking at `tt(s)` cannot see
anything, so the name is the only channel available. Length is not a virtue or a
vice — it is a payment for distance.

## Consequences

### Positive

- Short loops stay on one line and keep their shape, which is how loops read.
- Non-local names carry enough meaning to be recognised at the call site without
  navigating to the declaration.
- Long, distinctive non-local names collide with each other far less often.

### Negative

- "Common" and "conventional" are judgements, and a convention obvious to one
  reader is opaque to another; the rule cannot settle an argument about a
  specific name.
- The rule interacts with function length: it says to lengthen the names in a
  long function rather than to shorten the function, which can make an
  overlong function marginally more tolerable and so easier to leave alone.
- Short local names are more likely to shadow something, and more likely to look
  alike, both of which other rules then have to fix.

### Neutral

- This is about length, not about case or separators; that is a layout matter
  and is decided elsewhere.
- A short name that is unconventional in its role — `n` for a string — is
  outside what this rule permits, however local it is.

## Enforcement

Check the length of local and non-local names, and take function length into
account when deciding which a name is.

## Related

- [0178](0178-es-8-avoid-similar-looking-names.md) — the failure mode that short
  names make more likely.
- [0182](0182-es-12-do-not-reuse-names-in-nested-scopes.md) — short local names
  are the ones most likely to be reused in an inner scope.
- [0009](0009-p-3-express-intent.md) — a name is the cheapest place to express
  intent.
- [0034](0034-i-23-keep-the-number-of-function-arguments-low.md) — the long
  parameter list in the last example is a separate problem with the same cause.
