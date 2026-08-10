# 0371. CPL.3: If you must use C for interfaces, use C++ in the calling code using such interfaces

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section CPL (C-style programming), transcribed 2026-08-10

## Context

An interface is sometimes forced to be C: a third-party C library, a system
call interface, or a boundary that must be callable from a compiler other than
the one that built this side of it. That constraint applies to the interface,
not to the code on either side of it. C++ is more expressive than C and offers
better support for many kinds of programming, so the calling code should be
C++.

The recommended arrangement is to define the low-level interface in the common
subset of C and C++, for better type checking, and then — wherever possible —
encapsulate that low-level interface behind an interface that follows the C++
guidelines, for better abstraction, memory safety and resource safety. Only the
wrapper touches the C interface; the rest of the program uses the C++ one.

C can be called from C++ by declaring the function with C linkage:

```cpp
// in C:
double sqrt(double);
// in C++:
extern "C" double sqrt(double);
sqrt(2);
```

C++ can be called from C by giving a C++ definition C linkage, which lets the C
side see an ordinary C function while the body does whatever C++ it likes:

```cpp
// in C:
X call_f(struct Y*, int);
// in C++:
extern "C" X call_f(Y* p, int i)
{
    return p->f(i);   // possibly a virtual function call
}
```

## Decision

Where an interface is C, keep it as thin as possible and write everything that
uses it in C++.

- Declare the C interface with `extern "C"` and keep its declarations in the
  common subset of C and C++, so the C++ compiler type-checks them.
- Wrap that interface in a C++ one that owns the resources, converts the error
  codes, and presents the types the rest of the program actually uses. That
  wrapper is where the C library stops.
- Do not let raw handles, `void*`, ownership-by-convention, or C error codes
  from the underlying library escape past the wrapper.
- When exporting a function for C to call, define it in C++ with `extern "C"`
  linkage rather than writing the implementation in C.
- Restrict the wrapper's own signature to a C-style subset only where it is a
  genuine cross-compiler ABI boundary, per
  [0037](0037-i-26-if-you-want-a-cross-compiler-abi-use-a-c-style-subset.md);
  an internal wrapper over a C library is not one.

## Rationale

`extern "C"` constrains linkage and calling convention, and nothing else. It
does not require the definition to be C, so the only reason to write C on either
side of such a boundary is habit. The `call_f` example makes the point exactly:
the C caller sees a plain function, and the body performs a virtual call.

The wrapper is where the value is. A C library hands out resources that must be
released by an explicit call, and errors that are ignorable integers; converting
both once, at one place, is what makes the rest of the program able to obey the
rest of these records at all.

## Consequences

### Positive

- The C surface shrinks to a declaration file and a wrapper, instead of
  spreading through every caller.
- Resource release becomes a destructor's job rather than a discipline, so the
  library's cleanup call cannot be forgotten on an early return.
- The C library's error codes are converted once, into whatever error channel
  the program uses, instead of at every call site.

### Negative

- The wrapper is code with no function beyond translation: it must be written,
  tested and kept in step with the library it wraps, and it adds a layer to read
  through when debugging.
- A thin wrapper leaks the C model anyway; a thick one duplicates the library's
  design decisions and can be wrong about them.
- `extern "C"` functions cannot be overloaded and cannot propagate exceptions
  out to a C caller, so the boundary constrains the C++ side more than the rule
  admits.
- "Wherever possible" is doing a lot of work. Encapsulating a large C API is a
  substantial project, and the rule gives no guidance on doing it partially.

### Neutral

- Calling a C interface that reports failure through an error code is not itself
  a violation of the rules on error handling; the code is translated at the
  wrapper, which is the point of having one.
- The rule constrains the language of the calling code, not the choice to depend
  on a C library at all.

## Enforcement

None needed.

## Related

- [0369](0369-cpl-1-prefer-c-to-c.md) — the general preference this applies at a
  forced-C boundary.
- [0370](0370-cpl-2-if-you-must-use-c-use-the-common-subset-of-c-and-c-and-compile-the-c-code-as-c.md)
  — the low-level declarations belong in the common subset.
- [0037](0037-i-26-if-you-want-a-cross-compiler-abi-use-a-c-style-subset.md) —
  when the C-style restriction applies to one's own interfaces rather than to a
  library's.
- [0017](0017-p-11-encapsulate-messy-constructs-rather-than-spreading-through-the-code.md)
  — the wrapper is the encapsulation that rule asks for.
