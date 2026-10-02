# 0446. F.9: Unused parameters should be unnamed

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section F (Functions), transcribed 2026-08-10

## Context

The reasons are readability and the suppression of unused-parameter warnings.

```cpp
widget* find(const set<widget>& s, const widget& w, Hint);   // once upon a time, a hint was used
```

Allowing parameters to be unnamed was introduced in the early 1980s to address
exactly this problem: a parameter that must remain in the signature — because
callers pass it, because an overload set or a virtual interface requires it —
but which the body no longer touches.

Where a parameter is only conditionally unused, name it and declare it with the
`[[maybe_unused]]` attribute instead:

```cpp
template <typename Value>
Value* find(const set<Value>& s, const Value& v, [[maybe_unused]] Hint h)
{
    if constexpr (sizeof(Value) > CacheSize)
    {
        // a hint is used only if Value is of a certain size
    }
}
```

## Decision

Omit the name of a parameter the body does not use.

- Write the type alone when the parameter exists only to satisfy a signature: an
  overridden virtual, a callback signature, a tag, an overload discriminator.
- Use `[[maybe_unused]]` with a name where the parameter is used on some
  instantiations or configurations and not others.
- Do not silence the warning by casting the parameter to `void`, and do not
  silence it by naming it something apologetic.
- Do not leave a parameter in the signature at all if nothing requires it; an
  unused parameter that no interface forces is a parameter to delete.

## Rationale

The unused-parameter warning is worth keeping switched on, and this rule is the
price of keeping it useful. In a build with warnings as errors an unused
parameter has to be dealt with somehow, and dropping the name is the only option
that leaves the reader better informed than before: an unnamed parameter states
in the signature that the body ignores it, so nobody goes looking for the use.
A `void` cast or a placeholder name hides that same fact from everyone.

## Consequences

### Positive

- The signature itself records that the argument is ignored, which is a fact
  callers benefit from knowing.
- Unused-parameter warnings stay enabled and stay meaningful, so a genuinely
  forgotten parameter still gets caught.
- No dead name is left in scope for a later edit to accidentally use.

### Negative

- The declaration and the definition drift apart in readability: documentation
  and headers usually still name the parameter, so the reader of the definition
  has to look elsewhere to learn what the argument means.
- In a long parameter list, an unnamed one makes positional reading harder,
  since the surrounding names no longer line up with the arguments.
- `[[maybe_unused]]` is the correct tool for the conditional case but it also
  disables the warning permanently, so a parameter that later becomes fully
  unused is not reported.
- Removing the name is a local fix that can mask a design problem: the honest
  answer is often that the interface should not have had the parameter.

### Neutral

- The rule says nothing about which parameters ought to exist; it is about
  spelling, once existence is settled.
- A comment naming the parameter at the definition is a reasonable compromise
  and is what the upstream example does.

## Enforcement

- Flag named unused parameters.

## Related

- [0009](0009-p-3-express-intent.md) — an unnamed parameter states the intent
  "this is deliberately ignored".
- [0018](0018-p-12-use-supporting-tools-as-appropriate.md) — the rule exists so
  that a compiler warning stays worth enabling.
- [0034](0034-i-23-keep-the-number-of-function-arguments-low.md) — an unused
  parameter is often one that should be removed outright.
- [0020](0020-i-1-make-interfaces-explicit.md) — the signature is where facts
  about a function belong.
