# 0468. F.50: Use a lambda when a function won't do (to capture local variables, or to write a local function)

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section F (Functions), transcribed 2026-08-10

## Context

Functions cannot capture local variables and cannot be defined at local scope.
Where either is needed, a lambda is the tool, and a handwritten function object
is the fallback where a lambda will not serve. On the other hand, lambdas and
function objects do not overload; where overloading is needed a function is the
tool, because the workarounds that make lambdas overload are ornate. If either
will work, prefer writing a function: use the simplest tool necessary.

```cpp
// writing a function that should only take an int or a string
// -- overloading is natural
void f(int);
void f(const string&);

// writing a function object that needs to capture local state and appear
// at statement or expression scope -- a lambda is natural
vector<work> v = lots_of_work();
for (int tasknum = 0; tasknum < max; ++tasknum) {
    pool.run([=, &v] {
        /*
        ...
        ... process 1 / max - th of v, the tasknum - th chunk
        ...
        */
    });
}
pool.join();
```

The exception is generic lambdas, which offer a concise way to write function
templates and so can be useful even when a normal function template would do
equally well with a little more syntax.

## Decision

Choose between a function and a lambda by what the code needs, not by which is
fashionable.

- Write a lambda when the callable must capture local state, or must be defined
  where it is used because it exists only for that one call.
- Write a named function when neither is required, and in particular when the
  callable is to be overloaded, declared in a header, unit-tested on its own, or
  used from more than one place.
- Write a handwritten function object only where a lambda cannot express what is
  needed — a member typedef, a named type, a partial specialisation.
- Do not write a named non-generic lambda at namespace scope that captures
  nothing. That is a function with extra syntax; write the function.
- Be explicit about what is captured, and mind the lifetime of the captures; see
  [0470](0470-f-52-prefer-capturing-by-reference-in-lambdas-that-will-be-used-locally-including-passed-to-algorithms.md)
  and
  [0471](0471-f-53-avoid-capturing-by-reference-in-lambdas-that-will-be-used-non-locally-including-returned-stored-on-the-heap-or-passed-to-another-thread.md).

## Rationale

The rule is a tie-break, and its content is the tie-breaking direction: when
both work, the function wins. A function has a name, a declaration, a place to
hang a comment, an address that can be taken, and a set of overloads; a lambda
has none of these until someone gives it a variable name, at which point it is a
function written awkwardly.

What a lambda has that a function cannot have is capture and locality, and those
two capabilities are the entire justification for using one. Naming them as the
criterion keeps the choice from being made by preference.

## Consequences

### Positive

- Callables that could be reused end up with names, so they can be found,
  tested, and documented.
- Code that genuinely depends on local state stays next to that state instead of
  being lifted out into a file-scope helper with five parameters.
- The overloading limitation is surfaced early, rather than discovered when a
  second argument type is needed.

### Negative

- The criterion is a judgement, not a check. "Either will work" describes most
  short callables, and reasonable people will place the line differently.
- Preferring a function moves the code away from its context; a predicate
  hoisted to namespace scope needs its captured state passed as parameters,
  which conflicts with
  [0034](0034-i-23-keep-the-number-of-function-arguments-low.md).
- The generic-lambda exception is broad enough to swallow much of the rule,
  since `auto` parameters make a lambda a template with less syntax than the
  function template it replaces.
- The `[=, &v]` capture in the example is the very default capture that
  [0472](0472-f-54-if-you-capture-this-capture-all-variables-explicitly-no-default-capture.md)
  restricts inside a member function, so the two rules must be read together.

### Neutral

- Nothing here is about performance. A lambda and an equivalent function object
  compile to the same code.
- The upstream note that generic lambdas will lose their advantage once all
  functions can take concept parameters is a prediction, not a rule; C++20
  abbreviated function templates already narrow the gap.

## Enforcement

- Warn on the use of a named non-generic lambda that captures nothing and
  appears at global scope, for example
  `auto x = [](int i) { /*...*/; };`. Write an ordinary function instead.

## Related

- [0470](0470-f-52-prefer-capturing-by-reference-in-lambdas-that-will-be-used-locally-including-passed-to-algorithms.md)
  — how to capture once a lambda is the right choice and stays local.
- [0471](0471-f-53-avoid-capturing-by-reference-in-lambdas-that-will-be-used-non-locally-including-returned-stored-on-the-heap-or-passed-to-another-thread.md)
  — how to capture when it escapes, as in the thread-pool example above.
- [0472](0472-f-54-if-you-capture-this-capture-all-variables-explicitly-no-default-capture.md)
  — the restriction on default capture inside a member function.
- [0009](0009-p-3-express-intent.md) — using the simplest tool that says what is
  meant.
- [0561](0561-write-searches-as-named-loops-and-give-every-predicate-a-name.md) — amends this record: no lambda as a local function; lambdas only as one-line adapters or API callbacks.
