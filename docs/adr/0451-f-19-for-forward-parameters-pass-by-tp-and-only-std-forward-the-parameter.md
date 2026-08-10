# 0451. F.19: For "forward" parameters, pass by TP&& and only std::forward the parameter

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section F (Functions), transcribed 2026-08-10

## Context

If the object is to be passed onward to other code and not directly used by this
function, the function should be agnostic to the argument's constness and
rvalue-ness.

In that case, and only in that case, make the parameter `TP&&`, where `TP` is a
template type parameter. Such a parameter both ignores and preserves constness
and rvalue-ness. Any code that uses a `TP&&` is therefore implicitly declaring
that it does not itself care about the variable's constness or rvalue-ness,
because those are ignored, but that it intends to pass the value onward to other
code that does care, because they are preserved. As a parameter, `TP&&` is safe:
any temporary objects passed from the caller live for the duration of the call.
A parameter of type `TP&&` should essentially always be passed onward via
`std::forward` in the body.

```cpp
template<class F, class... Args>
inline auto invoke(F f, Args&&... args)
{
    return f(forward<Args>(args)...);
}
```

## Decision

Use `TP&&` only for parameters the function forwards, and forward them exactly
once.

- Make the parameter `TP&&` only where `TP` is a template type parameter
  deduced from the argument; a concrete `X&&` is a different rule.
- Apply `std::forward<TP>` to the parameter exactly once on every static path
  through the function.
- Do not read, copy, compare, or otherwise use a forwarding parameter for
  anything but forwarding; the function has declared it does not care what the
  argument is.
- Do not use `std::move` on a forwarding parameter, which would steal from an
  lvalue the caller still owns.
- Do not reach for perfect forwarding in non-generic code as an optimisation;
  the ordinary passing rules apply there.

## Rationale

`TP&&` is a statement about ignorance, and that is what makes the "only forward
it" restriction non-negotiable. The parameter may be bound to a `const` lvalue,
a mutable lvalue, or a temporary, and the function has explicitly declined to
know which; any use other than forwarding is therefore code written without
knowing what it is operating on. Forwarding exactly once matters for the same
reason: a second forward can pass an object that a previous forward already
moved out of.

## Consequences

### Positive

- One function template serves every combination of constness and value category
  without a combinatorial set of overloads.
- The value category the caller supplied survives the intermediate call, so a
  temporary is still a temporary at the destination.
- Temporaries are guaranteed to outlive the call, so a forwarding parameter has
  no lifetime hazard of its own.

### Negative

- A forwarding reference greedily matches almost anything, so overloading a
  function template that takes one is a known trap that hijacks calls intended
  for other overloads, including copy constructors.
- `TP&&` and `X&&` are spelled the same and behave differently, and the error is
  invisible at the point of use.
- Error messages from a mis-instantiated forwarding template are among the worst
  the language produces, and the constraint that would improve them has to be
  written by hand as a concept.
- Perfect forwarding pushes the function into a header and makes it a template
  whether or not genericity was wanted.

### Neutral

- The `Args&&...` variadic form is the same rule applied to a pack, and the same
  restriction holds per element.
- This is an advanced technique under the umbrella rule, expected to appear only
  in genuinely generic code.

## Enforcement

- Flag a function that takes a `TP&&` parameter, where `TP` is a template type
  parameter name, and does anything with it other than `std::forward`ing it
  exactly once on every static path.

## Related

- [0450](0450-f-18-for-will-move-from-parameters-pass-by-x-and-std-move-the-parameter.md)
  — the concrete-type `&&` this is routinely confused with.
- [0447](0447-f-15-prefer-simple-and-conventional-ways-of-passing-information.md)
  — the rule that classifies forwarding as advanced.
- [0028](0028-i-9-if-an-interface-is-a-template-document-its-parameters-using-concepts.md)
  — constraining the template parameter is what makes a forwarding template
  readable and its errors survivable.
- [0448](0448-f-16-for-in-parameters-pass-cheaply-copied-types-by-value-and-others-by-reference-to-const.md)
  — the warning against `T&&` "for efficiency" in non-generic code.
