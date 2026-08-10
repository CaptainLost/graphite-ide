# 0300. E.15: Throw by value, catch exceptions from a hierarchy by reference

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section E (Error handling), transcribed 2026-08-10

## Context

Throwing by value rather than by pointer, and catching by reference, prevents
copying and in particular prevents slicing base subobjects.

```cpp
void f()
{
    try {
        // ...
        throw new widget{}; // don't: throw by value not by raw pointer
        // ...
    }
    catch (base_class e) {  // don't: might slice
        // ...
    }
}
```

Instead, use a reference:

```cpp
catch (base_class& e) { /* ... */ }
```

or, typically better still, a `const` reference:

```cpp
catch (const base_class& e) { /* ... */ }
```

Most handlers do not modify their exception, and `const` is the general
recommendation.

Catch by value can be appropriate for a small value type such as an enumeration
value, where there is no hierarchy and therefore nothing to slice.

To rethrow a caught exception, use `throw;` and not `throw e;`. `throw e;` would
throw a new copy of `e`, sliced to its static type, instead of rethrowing the
original exception of its dynamic type. Both halves of that matter: the copy
costs, and the slice loses the derived part, which is where the information was.
This is also a reason to catch and rethrow less often; see
[0302](0302-e-17-dont-try-to-catch-every-exception-in-every-function.md) and
[0303](0303-e-18-minimize-the-use-of-explicit-try-catch.md).

Throwing a raw pointer has a second problem beyond copying: ownership. `throw
new widget{}` obliges every handler to know whether it must delete, and the
handler that catches `...` cannot, so the allocation leaks on exactly the path
that was least anticipated.

## Decision

Throw values and catch references.

- Throw a value: `throw MyError{...};`. Never throw a pointer, raw or smart.
- Catch by `const` reference where the exception belongs to a hierarchy:
  `catch (const std::exception& e)`.
- Catch by non-`const` reference only where the handler genuinely modifies the
  exception object before rethrowing.
- Catch by value only for a small standalone value type such as an `enum class`,
  where there is no base subobject to slice.
- Rethrow with `throw;`, never `throw e;`.

## Rationale

Catching by value is wrong in a way that produces no diagnostic and no crash. It
compiles, it runs, the handler executes, and the only symptom is that the
derived part of the exception — which is to say the specific information the
thrower went to the trouble of packaging — is gone. A handler that logs
`e.what()` after slicing will print the base class's message and look correct.

Throwing by pointer is the same error in the ownership dimension: the handler
that catches everything cannot know what to delete, so the least-anticipated
path is the one that leaks.

## Consequences

### Positive

- The dynamic type survives to the handler, so the information carried in a
  derived exception type is actually available.
- No copy is made at the catch site, so a handler cannot fail while handling.
- Ownership never becomes the handler's problem, because there is nothing to
  delete.

### Negative

- The bad form compiles silently. `catch (base_class e)` is legal, and nothing
  in the code's behaviour points at the missing information.
- `throw;` versus `throw e;` is a one-character distinction with entirely
  different semantics, which is a poor thing to rely on human review to catch.
- Catch-by-value is legitimate for enumerations, so a checker cannot simply flag
  all of them, and the exception weakens the rule as a mechanical check.
- Catch sites are among the least exercised code in a program, so an error made
  at one can survive a long time without being observed by anybody.
- Catching by reference binds the handler to an object whose storage the
  implementation owns; that object does not outlive the handler, so anything
  the handler wants to keep must be copied out deliberately.

### Neutral

- `const` versus non-`const` reference is a preference, not a correctness
  requirement; non-`const` is right when the handler modifies before rethrowing.
- The rule concerns the throw and catch expressions only, not the design of the
  exception type; that is
  [0299](0299-e-14-use-purpose-designed-user-defined-types-as-exceptions-not-built-in-types.md).

## Enforcement

- Flag catching by value of a type that has a virtual function.
- Flag throwing raw pointers.

## Related

- [0299](0299-e-14-use-purpose-designed-user-defined-types-as-exceptions-not-built-in-types.md)
  — the types this rule governs the handling of.
- [0310](0310-e-31-properly-order-your-catch-clauses.md) — the other way a
  hierarchy of handlers goes wrong.
- [0303](0303-e-18-minimize-the-use-of-explicit-try-catch.md) — the fewer catch
  sites there are, the fewer places this can be got wrong.
