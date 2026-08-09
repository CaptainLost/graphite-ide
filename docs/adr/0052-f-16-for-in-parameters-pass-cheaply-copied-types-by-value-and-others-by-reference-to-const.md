# 0052. F.16: For in parameters, pass cheaply-copied types by value and others by reference to const

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-10

The rule is stated upstream with quotation marks around "in"; the title here
drops them so that the filename stays clean.

## Context

The most frequently applicable rule in the whole F section for this codebase.
Nearly every function in `Fs`, `Sln` and `Text` takes a
`const std::filesystem::path&` or a `std::string_view`, which is this rule
followed.

## Decision

Adopt F.16 as written. Transcribed in full:

> **Reason.** Both let the caller know that a function will not modify the
> argument, and both allow initialization by rvalues.
>
> What is "cheap to copy" depends on the machine architecture, but two or three
> words (doubles, pointers, references) are usually best passed by value. When
> copying is cheap, nothing beats the simplicity and safety of copying, and for
> small objects (up to two or three words) it is also faster than passing by
> reference because it does not require an extra indirection to access from the
> function.

**Example.**

```cpp
void f1(const string& s);  // OK: pass by reference to const; always cheap

void f2(string s);         // bad: potentially expensive

void f3(int x);            // OK: Unbeatable

void f4(const int& x);     // bad: overhead on access in f4()
```

For advanced uses (only), where you really need to optimize for rvalues passed
to "input-only" parameters:

- If the function is going to unconditionally move from the argument, take it by
  `&&`. See F.18.
- If the function is going to keep a locally modifiable copy of the argument
  only for its own local use, taking it by value is fine.
- If the function is going to keep a copy of the argument to pass to another
  destination (to another function, or store in a non-local location), in
  addition to passing by `const&` (for lvalues), add an overload that passes the
  parameter by `&&` (for rvalues) and in the body `std::move`s it to its
  destination. Essentially this overloads a "will-move-from"; see F.18.
- In special cases, such as multiple "input + copy" parameters, consider using
  perfect forwarding. See F.19.

**Example.**

```cpp
int multiply(int, int); // just input ints, pass by value

// suffix is input-only but not as cheap as an int, pass by const&
string& concatenate(string&, const string& suffix);

void sink(unique_ptr<widget>);  // input only, and moves ownership of the widget
```

Avoid "esoteric techniques" such as passing arguments as `T&&` "for efficiency".
Most rumors about performance advantages from passing by `&&` are false or
brittle (but see F.18 and F.19).

**Notes.** A reference can be assumed to refer to a valid object (language rule).
There is no (legitimate) "null reference." If you need the notion of an optional
value, use a pointer, `std::optional`, or a special value used to denote "no
value."

**Enforcement.**

- (Simple) ((Foundation)) Warn when a parameter being passed by value has a size
  greater than `4 * sizeof(void*)`. Suggest using a reference to `const`
  instead.
- (Simple) ((Foundation)) Warn when a parameter passed by reference to `const`
  has a size less or equal than `2 * sizeof(void*)`. Suggest passing by value
  instead.
- (Simple) ((Foundation)) Warn when a parameter passed by reference to `const`
  is moved.
- (Not simple) Note: A stricter enforcement would depend on the performance
  characteristics of the target architecture.

**Exception.** To express shared ownership using `shared_ptr` types, follow R.34
or R.36, depending on whether or not the function unconditionally takes a
reference to the argument.

## Rationale

Adopted as written. Its two size thresholds — pass by value up to two words, by
`const&` beyond four — give the rule a mechanical form, which is rare in this
part of the guidelines.

The Notes paragraph is load-bearing for this codebase and connects to
[0031](0031-i-12-declare-a-pointer-that-must-not-be-null-as-not-null.md): a
reference cannot be null, so "required" is expressed by `const&` and "optional"
by a pointer or `std::optional`. F.16 and I.12 give the same answer from
different directions.

`std::string_view` is the right form for a read-only character sequence and is
already used; it is cheap to copy and carries its length, which also serves
[0032](0032-i-13-do-not-pass-an-array-as-a-single-pointer.md).

## Consequences

### Positive

- Two checkable thresholds, both expressible as an analyser rule.
- Matches what the codebase already does, so it is descriptive rather than
  aspirational.

### Negative

- `const std::filesystem::path&` is the codebase's most common parameter and
  it is not obviously on the right side of the threshold — `path` holds a string
  and is well over four words, so `const&` is correct, but callers holding a
  `string` pay a construction at every call site.
- The middle enforcement point would flag passing small structs by `const&`,
  which is a habit easy to fall into and invisible without a tool.

### Neutral

- The advanced bullet list is deferred to F.18 and F.19; neither technique is in
  use here.

## Related

- [0051](0051-f-15-prefer-simple-and-conventional-ways-of-passing-information.md)
  — the previous rule.
- [0053](0053-f-17-for-in-out-parameters-pass-by-reference-to-non-const.md) — the
  next rule.
- [0031](0031-i-12-declare-a-pointer-that-must-not-be-null-as-not-null.md) — the
  same required-versus-optional distinction.
