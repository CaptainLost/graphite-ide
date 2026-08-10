# 0063. C.42: If a constructor cannot construct a valid object, throw an exception

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

Leaving behind an invalid object is asking for trouble.

```cpp
class X2 {
    FILE* f;
    // ...
public:
    X2(const string& name)
        :f{fopen(name.c_str(), "r")}
    {
        if (!f) throw runtime_error{"could not open" + name};
        // ...
    }
    void read();      // read from f
    // ...
};

void f()
{
    X2 file {"Zeno"}; // throws if file isn't open
    file.read();      // fine
    // ...
}
```

```cpp
class X3 {     // bad: the constructor leaves a non-valid object behind
    FILE* f;   // call is_valid() before any other function
    bool valid;
    // ...
public:
    X3(const string& name)
        :f{fopen(name.c_str(), "r")}, valid{false}
    {
        if (f) valid = true;
        // ...
    }
    bool is_valid() { return valid; }
    void read();   // read from f
    // ...
};

void f()
{
    X3 file {"Heraclides"};
    file.read();   // crash or bad read!
    // ...
    if (file.is_valid()) {
        file.read();
        // ...
    }
    else {
        // ... handle error ...
    }
    // ...
}
```

For a variable definition — on the stack, or as a member of another object —
there is no explicit function call from which an error code could be returned.
Leaving behind an invalid object and relying on users to check an `is_valid()`
function consistently before use is tedious, error-prone and inefficient.

There are domains, such as some hard real-time systems, where exception handling
is not sufficiently predictable from a timing perspective; there the
`is_valid()` technique must be used, and checked consistently and immediately to
simulate RAII.

If tempted by a post-constructor initialisation or two-stage initialisation
idiom, do not. If it is genuinely unavoidable, use a factory function. One
reason people have used `init()` functions rather than doing the work in a
constructor is to avoid code replication; delegating constructors and default
member initialisation do that better. Another has been to delay initialisation
until an object is needed; the answer to that is usually not to declare a
variable until it can be properly initialised.

## Decision

A constructor that cannot establish the class's invariant must not return
normally; it throws.

- A constructor either produces an object satisfying the class's invariant or
  does not produce an object at all.
- Where construction can fail, throw from the constructor, so that no name is
  ever bound to an object that is not usable.
- Do not add a `valid` flag and an `is_valid()` accessor to a type whose
  constructor could not do its job. That is the `X3` shape, and it makes misuse
  compile.
- Where exception handling is unavailable — the hard real-time case the rule
  concedes — the `is_valid()` technique is the fallback, and it must then be
  checked consistently and immediately after every construction, to simulate
  RAII.
- Do not reach for a post-constructor or two-stage initialisation idiom. Where
  the work genuinely cannot be done in a constructor, use a factory function,
  per [0071](0071-c-50-use-a-factory-function-if-you-need-virtual-behavior-during-initialization.md).
- Do not use two-stage initialisation to avoid duplicated setup code; use a
  delegating constructor or a default member initialiser instead.
- Do not declare a variable before it can be properly initialised.

## Rationale

`X3` fails not because it lacks a check but because the check is optional. The
constructor has produced an object that the language considers complete, so
every subsequent call is well-formed, and the one call that matters —
`file.read()` before the `if` — is exactly the one nothing prevents.

The reason the rule reaches for a `throw` rather than an error code is
structural rather than stylistic. A variable definition — on the stack, or in a
member initialiser list — is not a function call: there is no return value to
inspect and nowhere to put a code. An exception is the only mechanism the
language gives a constructor for failing without leaving something behind.

## Consequences

### Positive

- A type written under this record has no "constructed but unusable" state for
  callers to remember to check.
- The failure cannot be ignored by writing nothing, unlike a `valid` flag that
  every call site must remember to consult.
- The invariant established by
  [0061](0061-c-40-define-a-constructor-if-a-class-has-an-invariant.md) holds
  for every object that exists, without qualification.

### Negative

- The throw lands on paths with no `try` in sight — a member initialiser list, a
  static object's initialisation, a container constructing its elements — and
  the code that has to handle it may be several frames from the definition that
  failed.
- Throwing from a constructor means the object's destructor never runs, so
  anything acquired earlier in the same constructor must already be owned by a
  member that cleans up after itself; a raw handle acquired first and thrown
  past is a leak.
- The rule cannot be followed where exceptions cannot be used — hard real-time
  systems, some embedded environments, across a C ABI — and its own fallback is
  `is_valid()`, the shape the example spends its length rejecting.
- The factory function offered as the alternative is more machinery than a
  constructor: a private constructor, a static function, a return type able to
  express failure, and a caller that must unwrap it. It also does not compose
  downward, because a member initialiser list has nowhere to handle a failed
  result, so a class with such a member needs a factory of its own.

### Neutral

- The hard real-time concession is a limit on the mechanism, not a licence for
  the `X3` shape: where `is_valid()` must be used it is to be checked
  immediately, so the window in which an invalid object exists is one statement
  wide.
- The rule is about the constructor's outcome, not about how the object is
  subsequently used.

## Related

- [0029](0029-i-10-use-exceptions-to-signal-a-failure-to-perform-a-required-task.md)
  — the general rule for signalling a failure to perform a required task; this
  record is that rule applied to construction.
- [0062](0062-c-41-a-constructor-should-create-a-fully-initialized-object.md) —
  the rule this one completes.
- [0061](0061-c-40-define-a-constructor-if-a-class-has-an-invariant.md) — the
  invariant that failure would otherwise violate.
- [0071](0071-c-50-use-a-factory-function-if-you-need-virtual-behavior-during-initialization.md)
  — the sanctioned alternative where a constructor alone cannot do the job.
