# 0064. C.43: Ensure that a copyable class has a default constructor

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

If a concrete class is copyable, ensure that it also satisfies the rest of
"semiregular". Many language and library facilities rely on default constructors
to initialise their elements — `T a[10]` and `std::vector<T> v(10)` among them.
A default constructor also often simplifies the task of defining a suitable
moved-from state for a type that is copyable.

```cpp
class Date { // BAD: no default constructor
public:
    Date(int dd, int mm, int yyyy);
    // ...
};

vector<Date> vd1(1000);   // default Date needed here
vector<Date> vd2(1000, Date{Month::October, 7, 1885});   // alternative
```

The default constructor is only auto-generated if there is no user-declared
constructor, so it is impossible to initialise `vd1` above. The absence of a
default value can surprise users and complicate the type's use, so if one can be
reasonably defined, it should be.

`Date` is chosen to encourage thought: there is no natural default date, so the
example is non-trivial. `{0, 0, 0}` is not a valid date in most calendar
systems, so choosing it would introduce something like floating point's NaN.
However, most realistic date classes have a first date — 1 January 1970 is
popular — and making that the default is usually trivial.

```cpp
class Date {
public:
    Date(int dd, int mm, int yyyy);
    Date() = default;
    // ...
private:
    int dd = 1;
    int mm = 1;
    int yyyy = 1970;
    // ...
};

vector<Date> vd1(1000);
```

A class whose members all have default constructors implicitly gets one:

```cpp
struct X {
    string s;
    vector<int> v;
};

X x; // means X{ {}, {} }; that is the empty string and the empty vector
```

Built-in types are not properly default constructed, though:

```cpp
struct X {
    string s;
    int i;
};

void f()
{
    X x;    // x.s is initialized to the empty string; x.i is uninitialized
    cout << x.s << ' ' << x.i << '\n';
    ++x.i;
}
```

Statically allocated objects of built-in types are default initialised to zero,
but local ones are not. A compiler might appear to initialise a local built-in
variable in a debug build while an optimised build does not, so code like the
above can seem to work while relying on undefined behaviour. An explicit default
initialiser fixes it:

```cpp
struct X {
    string s;
    int i {};   // default initialize (to 0)
};
```

Classes that have no reasonable default construction are usually not copyable
either, so they do not fall under this guideline. A base class should not be
copyable and so does not necessarily need a default constructor. A class that
must acquire a caller-provided resource during construction — `std::lock_guard`,
for instance — often cannot have one, and is usually not copyable anyway.

A class that has a special state which must be handled separately by member
functions or users causes extra work and most likely more errors. Such a type
can naturally use that special state as its default constructed value, whether
or not it is copyable: `std::ofstream` is not copyable but does have a default
constructor going with a "not open" state, and copyable special-state types such
as smart pointers use `== nullptr` as their default. It is nonetheless
preferable for a default constructor to default to a meaningful state, as
`std::string`'s `""` and `std::vector`'s `{}` do.

## Decision

Give every copyable concrete type a default constructor with a meaningful value.

- If a type is copyable, provide a default constructor, explicitly with
  `= default` where a user-declared constructor has suppressed the implicit one.
- Choose a meaningful default rather than a sentinel. Prefer an empty container,
  an empty string, or a conventional first value over a state that every member
  function then has to special-case.
- Give every built-in member an in-class initialiser — `int i {};` — because
  neither implicit default construction nor a defaulted constructor will
  initialise it.
- Do not force a default constructor onto a type that has no sensible default;
  make it non-copyable instead, which is usually already true of resource
  handles and base classes.
- Where a special state is unavoidable, make it the default-constructed state
  rather than a second special value.

## Rationale

The rule is really about which types the standard library will accept. Sizing a
`vector`, declaring an array, using a type as a map's mapped type and resizing
all require default construction, so a copyable type without one is a value type
that cannot be stored the usual ways — an irregularity paid for at every use
site rather than once at the definition.

The built-in member trap is the part most worth carrying: a defaulted default
constructor leaves `int` and pointer members with indeterminate values, and the
resulting bug is one that a debug build can hide and an optimised build exposes.
That is a much sharper hazard than the missing constructor itself.

## Consequences

### Positive

- The type works with `vector` sizing, arrays, maps and every generic facility
  that default-constructs.
- A moved-from state has an obvious definition.
- In-class initialisers close the indeterminate-value hole that defaulted
  construction leaves.

### Negative

- Some types have no honest default, and providing one anyway invents a value
  that means "not really a value" — the NaN the guideline warns about — which
  every member function then has to consider.
- A default-constructed object that is valid but not useful weakens the
  guarantee of
  [0062](0062-c-41-a-constructor-should-create-a-fully-initialized-object.md):
  the object satisfies its invariant while not yet meaning anything.
- In-class initialisers on every built-in member cost an initialisation the
  caller may immediately overwrite, which is measurable for large aggregates
  built in a loop.

### Neutral

- Move-only types, base classes and resource handles are outside the rule rather
  than in violation of it.
- The special-state types in the standard library are precedent for using the
  special state as the default, where one is unavoidable.

## Enforcement

- Flag classes that are copyable by `=` but have no default constructor.
- Flag classes that are comparable with `==` but not copyable.

## Related

- [0049](0049-c-11-make-concrete-types-regular.md) — regularity, of which this
  is the semiregular part.
- [0062](0062-c-41-a-constructor-should-create-a-fully-initialized-object.md) —
  the default constructor is subject to it like any other.
- [0061](0061-c-40-define-a-constructor-if-a-class-has-an-invariant.md) — the
  default-constructed value must satisfy the invariant too.
- [0051](0051-c-20-if-you-can-avoid-defining-default-operations-do.md) — the
  implicit default constructor a user-declared constructor takes away.
