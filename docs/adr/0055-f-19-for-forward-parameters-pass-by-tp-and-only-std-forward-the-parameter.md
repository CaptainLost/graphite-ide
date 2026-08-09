# 0055. F.19: For forward parameters, pass by TP&& and only std::forward the parameter

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-10

## Context

Perfect forwarding appears nowhere in this repository. The only place it would
be a candidate is `EventBus`, which takes handlers and payloads generically —
and it stores them in `std::function` rather than forwarding them onward.

## Decision

Adopt F.19 as written. Transcribed in full:

> **Reason.** If the object is to be passed onward to other code and not directly
> used by this function, we want to make this function agnostic to the argument
> const-ness and rvalue-ness.
>
> In that case, and only that case, make the parameter `TP&&` where `TP` is a
> template type parameter — it both ignores and preserves const-ness and
> rvalue-ness. Therefore any code that uses a `TP&&` is implicitly declaring that
> it itself doesn't care about the variable's const-ness and rvalue-ness (because
> it is ignored), but that intends to pass the value onward to other code that
> does care about const-ness and rvalue-ness (because it is preserved). When used
> as a parameter `TP&&` is safe because any temporary objects passed from the
> caller will live for the duration of the function call. A parameter of type
> `TP&&` should essentially always be passed onward via `std::forward` in the
> body of the function.

**Example.** Usually you forward the entire parameter (or parameter pack, using
`...`) exactly once on every static control flow path:

```cpp
template<class F, class... Args>
inline decltype(auto) invoke(F&& f, Args&&... args)
{
    return forward<F>(f)(forward<Args>(args)...);
}
```

**Example.** Sometimes you may forward a composite parameter piecewise, each
subobject once on every static control flow path:

```cpp
template<class PairLike>
inline auto test(PairLike&& pairlike)
{
    // ...
    f1(some, args, and, forward<PairLike>(pairlike).first);           // forward .first
    f2(and, forward<PairLike>(pairlike).second, in, another, call);   // forward .second
}
```

**Enforcement.** Flag a function that takes a `TP&&` parameter (where `TP` is a
template type parameter name) and does anything with it other than
`std::forward`ing it exactly once on every static path, or `std::forward`ing it
more than once but qualified with a different data member exactly once on every
static path.

## Rationale

Adopted as written, and currently unexercised — which is the right state for it
given [0051](0051-f-15-prefer-simple-and-conventional-ways-of-passing-information.md):
perfect forwarding is an advanced technique that needs a demonstrated need, and
none has been demonstrated here.

The rule's value in this directory is its precision about *when* `TP&&` is
correct: only when the function does not use the argument itself but passes it
onward. That narrow condition is what distinguishes a forwarding reference from
the "`&&` for efficiency" habit F.16 explicitly warns against. Recording it now
means that if `EventBus` ever forwards a payload into a handler rather than
storing it, the shape is already decided.

## Consequences

### Positive

- States a precise condition, so a future `TP&&` can be judged rather than
  argued about.
- Reinforces F.16's warning against `&&` used as a performance gesture.

### Negative

- Forwarding references interact badly with overload resolution — a `TP&&`
  parameter is a greedy match that can swallow calls intended for another
  overload — and the rule does not mention this. Anyone applying it here should
  know that separately.
- The enforcement point is precise and entirely unavailable: no tool here checks
  forwarding paths.

### Neutral

- No deviation recorded; there is nothing in the repository this rule applies to
  yet.

## Related

- [0054](0054-f-18-for-will-move-from-parameters-pass-by-x-and-std-move-the-parameter.md)
  — the previous rule.
- [0052](0052-f-16-for-in-parameters-pass-cheaply-copied-types-by-value-and-others-by-reference-to-const.md)
  — F.16, which warns against `&&` used for efficiency.
- [0028](0028-i-9-if-an-interface-is-a-template-document-its-parameters-using-concepts.md)
  — `EventBus`, the one template interface this could touch.
