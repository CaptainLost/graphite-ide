# 0056. F.20: For out output values, prefer return values to output parameters

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-10

## Context

A scan of every header for non-`const` reference parameters finds one true
output parameter: `Window::FramebufferSize(int& width, int& height) const`.
Everything else the scan turns up — `FileTree::Expand(FileNode&)`,
`Editing::InsertText(OpenItem&, ...)`, `ImGuiRenderer::Init(Window&)` — is
in-out: the callee modifies an object the caller already owns, which is F.17's
subject, not this rule's.

## Decision

Adopt F.20 as written. Transcribed in full:

> **Reason.** A return value is self-documenting, whereas an `&` could be either
> in-out or out-only and is liable to be misused.
>
> This includes large objects like standard containers that use implicit move
> operations for performance and to avoid explicit memory management.
>
> If you have multiple values to return, use a `tuple` or similar multi-member
> type.

**Example.**

```cpp
// OK: return pointers to elements with the value x
vector<const int*> find_all(const vector<int>&, int x);

// Bad: place pointers to elements with value x in-out
void find_all(const vector<int>&, vector<const int*>& out, int x);
```

**Note.** A struct of many (individually cheap-to-move) elements might be in
aggregate expensive to move.

**Exceptions.**

- For non-concrete types, such as types in an inheritance hierarchy, return the
  object by `unique_ptr` or `shared_ptr`.
- If a type is expensive to move (e.g., `array<BigTrivial>`), consider
  allocating it on the free store and return a handle (e.g., `unique_ptr`), or
  passing it in a reference to non-`const` target object to fill (to be used as
  an out-parameter).
- To reuse an object that carries capacity (e.g., `std::string`, `std::vector`)
  across multiple calls to the function in an inner loop: treat it as an in/out
  parameter and pass by reference.

**Example.** Assuming that `Matrix` has move operations (possibly by keeping its
elements in a `std::vector`):

```cpp
Matrix operator+(const Matrix& a, const Matrix& b)
{
    Matrix res;
    // ... fill res with the sum ...
    return res;
}

Matrix x = m1 + m2;  // move constructor

y = m3 + m3;         // move assignment
```

**Note.** The return value optimization doesn't handle the assignment case, but
the move assignment does.

**Example.**

```cpp
struct Package {      // exceptional case: expensive-to-move object
    char header[16];
    char load[2024 - 16];
};

Package fill();       // Bad: large return value
void fill(Package&);  // OK

int val();            // OK
void val(int&);       // Bad: Is val reading its argument
```

**Enforcement.** Flag reference to non-`const` parameters that are not read
before being written to and are a type that could be cheaply returned; they
should be "out" return values.

## Rationale

Adopted as written. The rule is already the codebase's habit, and
[0004](0004-return-result-t-instead-of-throwing-across-module-boundaries.md) is
the main reason: a function that could have used an out parameter and a status
code returns `Result<T>` instead, which is this rule and I.10's goal met at once.

The enforcement point states the test precisely — a non-`const` reference that is
written before it is read is an out parameter — and that test is what separates
the one real finding from the dozen in-out parameters that share its shape.

`FramebufferSize` fails it: both `int&`s are written without being read, and two
`int`s are as cheap to return as anything can be. It is also, per F.21, a case
for a small struct rather than a `pair`.

## Consequences

### Positive

- One precise, checkable test, and a codebase that passes it nearly everywhere.
- The `Result<T>` scheme means most functions could not use an out parameter
  even if the author wanted to.

### Negative

- The third Exception — reuse a capacity-carrying object across calls in an
  inner loop — is a real optimisation this codebase forgoes, and forgoing it is
  invisible. `TextBuffer` operations that build strings per call would be the
  candidates.
- Returning by value relies on move semantics being present and correct; a type
  that accidentally suppresses its move operations turns every return into a
  copy with no diagnostic.

### Neutral

- The `unique_ptr` Exception for polymorphic types is already how
  `DirectoryWatcher::Create` works.

## Deviations to fix

| What | Where | Note |
|---|---|---|
| `FramebufferSize(int& width, int& height)` — two out parameters, both written without being read | `Core/src/Core/Platform/Window.h:37` | Should return a small struct; see [0057](0057-f-21-to-return-multiple-out-values-prefer-returning-a-struct.md). It is a virtual on an interface, so changing it touches `GlfwWindow` and every caller |

## Related

- [0055](0055-f-19-for-forward-parameters-pass-by-tp-and-only-std-forward-the-parameter.md)
  — the previous rule.
- [0057](0057-f-21-to-return-multiple-out-values-prefer-returning-a-struct.md) —
  the next rule, which says what to return here.
- [0053](0053-f-17-for-in-out-parameters-pass-by-reference-to-non-const.md) — the
  in-out case this rule is distinguished from.
