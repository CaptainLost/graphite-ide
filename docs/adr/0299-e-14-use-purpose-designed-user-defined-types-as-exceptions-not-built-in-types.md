# 0299. E.14: Use purpose-designed user-defined types as exceptions (not built-in types)

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section E (Error handling), transcribed 2026-08-10

## Context

A user-defined type can better transmit information about an error to a handler.
Information can be encoded into the type itself, and the type is unlikely to
clash with other people's exceptions.

```cpp
throw 7; // bad
throw "something bad";  // bad
throw std::exception{}; // bad - no info
```

Deriving from `std::exception` gives the flexibility to catch the specific
exception or to handle it generally through `std::exception`:

```cpp
class MyException : public std::runtime_error
{
public:
    MyException(const string& msg) : std::runtime_error{msg} {}
    // ...
};
// ...
throw MyException{"something bad"};  // good
```

Exceptions do not need to be derived from `std::exception`:

```cpp
class MyCustomError final {};  // not derived from std::exception
// ...
throw MyCustomError{};  // good - handlers must catch this type (or ...)
```

Library types derived from `std::exception` can be used as generic exceptions if
no useful information can be added at the point of detection:

```cpp
throw std::runtime_error("someting bad"); // good
// ...
throw std::invalid_argument("i is not even"); // good
```

`enum class` types are also allowed:

```cpp
enum class alert {RED, YELLOW, GREEN};
throw alert::RED; // good
```

The rule has two distinct reasons behind it, and they are worth separating. The
first is that the type is the channel: a handler dispatches on type, so a type
that means nothing in particular gives the handler nothing to dispatch on. The
second is uniqueness. `throw 7` will be caught by any `catch (int)` anywhere up
the stack, including one belonging to a library that has no idea this code
exists, and a distinct type makes that collision impossible.

## Decision

Throw a purpose-designed user-defined type, not a built-in one.

- Throw a user-defined type or a standard-library exception type, never an
  `int`, a string literal, or a bare `std::exception`.
- Derive from `std::exception` or one of its standard descendants when a generic
  handler should be able to catch it; use a standalone type when handlers should
  be forced to name it.
- Use a standard type such as `std::runtime_error` or `std::invalid_argument`
  where no useful information can be added at the point of detection.
- Encode the classification in the type, and the detail that varies per
  occurrence in the object. A handler branches on the first and reports the
  second.
- Do not throw a type that another component might plausibly throw for its own
  errors.

## Rationale

The type is what the handler selects on, so a type shared with unrelated code is
a channel shared with unrelated code. That is the argument `throw 7` cannot
answer: it is not merely uninformative, it is ambiguous in a way the author
cannot detect locally, since the `catch (int)` that intercepts it may be in a
library that has never heard of this code.

The same reasoning explains why classification belongs in the type rather than
in a message string. A handler can select on `std::invalid_argument`; it cannot
sensibly select on the text of `what()`, and any code that tries has coupled
itself to prose that was written to be read by a person.

## Consequences

### Positive

- Handlers dispatch on something that means what they think it means.
- Errors from different components cannot be confused for one another.
- Classification and detail are separated, so callers branch on the first and
  display the second.

### Negative

- A hierarchy of error types is code to write, maintain, and keep in some
  sensible relation, and most of it is boilerplate that carries no logic.
- Deciding between deriving from `std::exception` and standing alone commits the
  type's callers: the first admits a generic handler, the second forces every
  handler to name the type, and changing the answer later is a breaking change.
- An exception type must be copyable without failing, since propagation may copy
  it, so the very types that would most like to carry rich detail are the ones
  least able to. See
  [0301](0301-e-16-destructors-deallocation-swap-and-exception-type-copy-move-construction-must-never-fail.md).
- A hierarchy brings an ordering obligation with it at every catch site, and
  getting it wrong makes a handler unreachable silently. See
  [0310](0310-e-31-properly-order-your-catch-clauses.md).
- The rule cannot prevent the real ambiguity, which is two components using the
  *same* user-defined type for different meanings.

### Neutral

- `enum class` is explicitly permitted, so a small classification value is a
  legitimate exception type upstream.
- Standard exception types are acceptable when nothing useful can be added; the
  rule is against built-in types, not against reuse.

## Enforcement

- Catch the throwing of built-in types, and of `std::exception` itself.

## Related

- [0300](0300-e-15-throw-by-value-catch-exceptions-from-a-hierarchy-by-reference.md)
  — how such a type must be thrown and caught.
- [0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md) — the same
  argument applied to interfaces generally.
- [0310](0310-e-31-properly-order-your-catch-clauses.md) — a hierarchy of error
  types brings an ordering obligation with it.
