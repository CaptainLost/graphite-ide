# 0345. T.65: Use tag dispatch to provide alternative implementations of a function

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

A template defines a general interface. Tag dispatch allows an implementation
to be selected based on specific properties of an argument type, and to select
it at compile time, which is where the performance argument comes from.

This is a simplified version of `std::copy`, ignoring the possibility of
non-contiguous sequences:

```cpp
struct pod_tag {};
struct non_pod_tag {};

template<class T> struct copy_trait { using tag = non_pod_tag; };   // T is not "plain old data"
template<> struct copy_trait<int> { using tag = pod_tag; };         // int is "plain old data"

template<class Iter>
Out copy_helper(Iter first, Iter last, Iter out, pod_tag)
{
    // use memmove
}

template<class Iter>
Out copy_helper(Iter first, Iter last, Iter out, non_pod_tag)
{
    // use loop calling copy constructors
}

template<class Iter>
Out copy(Iter first, Iter last, Iter out)
{
    return copy_helper(first, last, out, typename copy_trait<Iter>::tag{});
}

void use(vector<int>& vi, vector<int>& vi2, vector<string>& vs, vector<string>& vs2)
{
    copy(vi.begin(), vi.end(), vi2.begin()); // uses memmove
    copy(vs.begin(), vs.end(), vs2.begin()); // uses a loop calling copy constructors
}
```

The user calls one function; overload resolution on the extra tag argument
picks the implementation. This is a general and powerful technique for
compile-time algorithm selection.

Where concepts are available, the same alternatives can be distinguished
directly, without the tag types or the trait:

```cpp
template<class Iter>
    requires Pod<Value_type<Iter>>
Out copy_helper(Iter first, Iter last, Out out)
{
    // use memmove
}

template<class Iter>
Out copy_helper(Iter first, Iter last, Out out)
{
    // use loop calling copy constructors
}
```

Concepts are a language feature in C++20, so constrained overloads are the first
choice wherever that standard is available. Tag dispatch remains the fallback
where the property being dispatched on is not expressible as a concept, or where
the set of alternatives must stay open to specialization by a user.

## Decision

Where one function must have several implementations chosen by a property of an
argument type, select between them at compile time rather than at run time.

- Prefer constrained overloads or `if constexpr` on a concept wherever C++20 is
  available.
- Where a concept will not do, define empty tag types, give a trait template a
  `tag` member naming one of them, and specialize the trait for the types that
  differ.
- Keep the tagged overloads out of the interface — name them `_helper` or place
  them in a `detail` namespace — and expose a single untagged entry point.
- Pass the tag as a value-initialised temporary of the trait's tag type, so the
  dispatch is decided by overload resolution and costs nothing at run time.

## Rationale

The alternative implementations differ in more than a branch: one calls
`memmove`, the other calls copy constructors, and only one of them is even
valid for a given element type. A run-time test cannot express that, because
both branches would have to compile. Tag dispatch turns the property into a
type and hands the choice to overload resolution, which is the one mechanism in
the language that can discard the invalid alternative rather than merely
skipping it.

## Consequences

### Positive

- The selection is free at run time and cannot be got wrong by a caller.
- Alternatives that would not compile for the other case never get
  instantiated.
- The trait is an open extension point: a user can opt their own type into the
  fast path by specializing it.

### Negative

- It is a lot of apparatus. Two tag types, a trait template, a specialization
  and a set of helper overloads stand between the caller and the code that
  runs, and every one of them appears in a diagnostic.
- The dispatch is invisible at the call site, so a performance-relevant choice
  is made somewhere the reader is not looking.
- Adding a third alternative means touching the tag set, the trait and every
  helper; the technique does not scale gracefully with the number of
  properties.
- Under C++20 most uses are superseded by constrained overloads, so new tag
  dispatch needs a reason why a concept would not serve — and the technique
  survives mainly in code that predates concepts.

### Neutral

- Traits and tags are also how the standard library reports properties of
  iterators, so the pattern is familiar even where it is verbose.
- The tag parameter is unnamed in the helpers, which is the conventional signal
  that it carries no value.

## Related

- [0344](0344-t-64-use-specialization-to-provide-alternative-implementations-of-class-templates.md)
  — the corresponding technique for class templates, and where the trait itself
  is specialized.
- [0028](0028-i-9-if-an-interface-is-a-template-document-its-parameters-using-concepts.md)
  — the concept-based form of the same selection.
- [0359](0359-t-121-use-template-metaprogramming-primarily-to-emulate-concepts.md)
  — tag dispatch is one of the concept emulations that C++20 retires.
- [0367](0367-t-144-dont-specialize-function-templates.md) — why the alternative
  implementations are overloads on a tag rather than specializations.
