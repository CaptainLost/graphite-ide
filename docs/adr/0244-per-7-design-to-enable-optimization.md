# 0244. Per.7: Design to enable optimization

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section Per (Performance), transcribed 2026-08-10

## Context

The initial design often needs to be optimised later, and a design that ignores
the possibility of later improvement is hard to change. The worked example is
`qsort`, from the C and C++ standards:

```cpp
void qsort(void* base, size_t num, size_t size, int (*compar)(const void*, const void*));
```

When did anyone want to sort memory? What is actually sorted is a sequence of
elements, typically stored in a container. A call to `qsort` throws away useful
information — the element type — forces the caller to repeat information already
known — the element size — and forces the caller to write extra code, such as a
function to compare two `double`s. That is added work for the programmer, it is
error-prone, and it deprives the compiler of information it needs to optimise:

```cpp
double data[100];
// ... fill data ...
// 100 chunks of memory of sizeof(double) starting at
// address data using the order defined by compare_doubles
qsort(data, 100, sizeof(double), compare_doubles);
```

C++98 could already do better:

```cpp
template<typename Iter>
    void sort(Iter b, Iter e);  // sort [b:e)
sort(data, data + 100);
```

Here the compiler's knowledge of the array's size, the element type and how to
compare `double`s is all retained. With C++20 it improves again:

```cpp
// sortable specifies that c must be a
// random-access sequence of elements comparable with <
void sort(sortable auto& c);
sort(c);
```

The key is to pass enough information for a good implementation to be chosen.
Even these still have a weakness: they rely implicitly on the element type
having `<` defined, so a second version taking a comparison is needed to
complete the interface, and the standard library provides it:

```cpp
// compare elements of c using p
template<random_access_range R, class C> requires sortable<R, C>
void sort(R&& r, C c);
```

A second example is about what an interface returns rather than what it takes.

```cpp
template<class ForwardIterator, class T>
bool binary_search(ForwardIterator first, ForwardIterator last, const T& val);
```

`binary_search(begin(c), end(c), 7)` says whether 7 is in `c`. It does not say
where it is, or whether there is more than one. Sometimes the minimal answer is
enough, but a good interface passes back the information the caller needs, so
the standard library also offers `lower_bound`, which returns an iterator to the
first match if any, otherwise to the first element greater than the value, or
`last`:

```cpp
template<class ForwardIterator, class T>
ForwardIterator lower_bound(ForwardIterator first, ForwardIterator last, const T& val);
```

And where even that is not enough, `equal_range`, which returns the first and
one-past-last match:

```cpp
template<class ForwardIterator, class T>
pair<ForwardIterator, ForwardIterator>
equal_range(ForwardIterator first, ForwardIterator last, const T& val);

auto r = equal_range(begin(c), end(c), 7);
for (auto p = r.first; p != r.second; ++p)
    cout << *p << '\n';
```

All three are implemented by the same basic code. They are three presentations
of one algorithm, ranging from making simple things simple to not hiding useful
information. Crafting such a set requires experience and domain knowledge.

Premature optimisation is famously called the root of all evil, but that is no
reason to despise performance. It is never premature to consider what makes a
design amenable to improvement, and improved performance is a commonly wanted
improvement. When writing a function that is not a one-off implementation
detail, the guidelines ask for attention to six things: passing enough
information through the interface for the implementation to be improved later;
using compact data such as `std::vector` by default and accessing it
systematically, hiding any linked structure from users; distinguishing mutable
from non-mutable data in argument passing and imposing neither a resource
management burden nor spurious run-time indirections on callers; not
overgeneralising, since a design that caters for every possible use and defers
every decision behind compile-time or run-time indirection becomes a bloated
mess, so generalise from concrete examples with zero-overhead generalisation as
the ideal; using libraries with good interfaces, and imitating a good library's
style when one must be built; and isolating messy or old-style code behind an
interface of one's own choosing so that bad designs do not bleed into the rest.

Three further notes. Do not simply craft the interface to match the first
implementation and the first use case that comes to mind; review it once the
initial implementation is complete, because after deployment mistakes are hard
to remedy. Things have costs — do not be paranoid about them, modern computers
really are very fast, but keep a rough sense of the order of magnitude of a
memory access, a function call, a string comparison, a system call, a disk
access and a network message. And if only one implementation can be imagined,
there is probably nothing here for which a stable interface can be devised; it
may be an implementation detail, but it is worth pausing to ask what interface
would be needed if the operation had to be implemented with multiple threads, or
vectorised.

This rule does not contradict the prohibition on premature optimisation. It
complements it, by encouraging developers to enable later, appropriate,
non-premature optimisation where it turns out to be needed.

## Decision

Design interfaces so that the implementation behind them can be improved later
without changing the callers.

- Pass enough information through the interface for a better implementation to
  be chosen: the element type, the extent, the comparison. Do not reduce data to
  `void*` and a byte count.
- Do not make the caller supply information the compiler already has, such as
  `sizeof` an element, or write adapter code such as a comparison function for a
  built-in type.
- Return the information the caller needs, and offer the fuller form alongside
  the simple one where both are useful, as `binary_search`, `lower_bound` and
  `equal_range` do.
- Default to compact, contiguous storage such as `std::vector`, and keep any
  linked structure out of the interface so it can be replaced.
- Do not impose a resource-management burden or a spurious indirection on
  callers; pass arguments in the conventional ways, because unconventional
  "optimised" ones complicate later reimplementation.
- Generalise from concrete cases and stop there. Do not add indirection for uses
  that have not appeared.
- Isolate messy or old-style code behind an interface of your own, per
  [0039](0039-i-30-encapsulate-rule-violations.md).
- Review the interface once the first implementation exists, before it has
  callers to break.

## Rationale

The distinction that makes this rule compatible with
[0239](0239-per-2-dont-optimize-prematurely.md) is between optimising an
implementation and preserving the freedom to optimise it. The first is work with
a cost and no established benefit; the second is usually free at design time and
unaffordable afterwards.

`qsort` is the argument in one line. Nothing is wrong with its algorithm; the
interface simply discarded the element type, and no improvement to the
implementation can recover it. Once callers exist, that loss is permanent.

## Consequences

### Positive

- Implementations can be replaced — vectorised, parallelised, given different
  storage — without touching callers.
- Interfaces that retain type information are also safer and shorter to call,
  so the performance argument and the correctness argument agree.
- Asking what a threaded or vectorised implementation would need surfaces
  interface defects while they are still cheap to fix.

### Negative

- The line between designing for optimisation and optimising early is a matter
  of experience, and the rule admits as much. In practice it is used to justify
  work on both sides of that line.
- Offering simple and complete versions of an operation, as with
  `binary_search`/`lower_bound`/`equal_range`, means more interface to document,
  test and keep consistent.
- Designing for a future need risks the overgeneralisation the same rule warns
  against; the two cautions have to be balanced case by case with no test to
  decide.
- Reviewing and revising an interface after the first implementation is real
  rework, paid at a point where the code already appears finished.

### Neutral

- Not every function needs a stable interface; implementation details are
  exempt, and the rule says so.
- Keeping a rough sense of the cost of a memory access, a call, a system call or
  a network round trip is design intuition, not a performance claim requiring
  measurement.

## Enforcement

- Tricky in general. Looking for `void*` function arguments will find some
  interfaces that hinder later optimisation.

## Related

- [0239](0239-per-2-dont-optimize-prematurely.md) — the rule this one
  complements, and the tension it has to be read against.
- [0032](0032-i-13-do-not-pass-an-array-as-a-single-pointer.md) — the
  `(pointer, count)` interface this rule rejects for a second reason.
- [0028](0028-i-9-if-an-interface-is-a-template-document-its-parameters-using-concepts.md)
  — `sortable auto& c` is that rule and this one at the same time.
- [0251](0251-per-16-use-compact-data-structures.md) — the compact-data habit
  this rule asks for by default.
