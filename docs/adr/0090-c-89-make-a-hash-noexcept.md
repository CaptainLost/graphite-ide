# 0090. C.89: Make a hash noexcept

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

Users of hashed containers use the hash indirectly and do not expect simple
access to throw. It is also a standard-library requirement, not merely a
convention.

```cpp
template<>
struct hash<My_type> {  // thoroughly bad hash specialization
    using result_type = size_t;
    using argument_type = My_type;
    size_t operator()(const My_type & x) const
    {
        size_t xs = x.s.size();
        if (xs < 4) throw Bad_My_type{};    // "Nobody expects the Spanish inquisition!"
        return hash<size_t>()(x.s.size()) ^ trim(x.s);
    }
};

int main()
{
    unordered_map<My_type, int> m;
    My_type mt{ "asdfg" };
    m[mt] = 7;
    cout << m[My_type{ "asdfg" }] << '\n';
}
```

Nothing at the call site suggests that `m[mt]` might exit by an exception. The
hash is invoked from inside the container's lookup and insertion machinery,
where an unexpected throw arrives during a rehash or a bucket walk and is
awkward to reason about.

If a hash specialisation has to be written, the guidance is to combine
standard-library `hash` specialisations with `^` (exclusive or). That tends to
work better, for a non-specialist, than attempting cleverness.

## Decision

Declare every `hash` specialisation and every hash function object `noexcept`,
and write a body that cannot fail.

- Mark `operator()` on a hash type `const noexcept`.
- Do not validate the argument inside the hash and do not throw on a value the
  hash considers malformed; a hash function's job is to produce a number, not to
  police an invariant.
- Build a composite hash by combining `std::hash` specialisations of the members
  with `^`, rather than inventing a mixing scheme.
- Do not allocate, format, or lock inside a hash, because those are the
  operations that introduce a failure path.

## Rationale

The sharpest argument is that the throw is invisible from where it is felt.
`m[mt]` looks like an indexing operation, and a hash that throws turns every
container operation on that type into a fallible one without changing a single
signature the caller reads.

The standard library also requires it: the standard's requirements on a hash
function say it shall not throw, so a throwing specialisation breaks the
contract the unordered containers are written against and the behaviour of any
container using it is undefined. This is less a style preference than a
conformance requirement.

## Consequences

### Positive

- Container operations on a hashed key type keep their obvious, non-failing
  meaning.
- `noexcept` is checked, so a hash that acquires a throwing dependency later
  fails to compile rather than failing at run time.
- Combining member hashes with `^` gives a short, reviewable implementation.

### Negative

- The hash can no longer report that its argument is malformed, so a class whose
  invariant is genuinely violable must enforce that invariant elsewhere, before
  the object ever reaches a container.
- `^` is a weak combiner: it maps equal member values to zero and ignores field
  order, so `{a, b}` and `{b, a}` collide. The rule trades hash quality for
  simplicity and safety.
- `noexcept` on a function that does allocate is a promise the compiler cannot
  fully verify; violating it terminates the process instead of unwinding, which
  is a harsher failure than the throw it replaced.

### Neutral

- The rule constrains the hash, not the equality operator, though the two must
  stay consistent.
- Nothing here says a type must be hashable; it says that if it is, hashing must
  not fail.

## Enforcement

- Flag throwing hashes.

## Related

- [0013](0013-p-7-catch-run-time-errors-early.md) — validate the value before it
  reaches the container, not inside the hash.
- [0093](0093-c-101-give-a-container-value-semantics.md) — the same familiarity
  argument applied to containers rather than to hashes.
