# 0019. P.13: Use support libraries as appropriate

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section P (Philosophy), transcribed 2026-08-10

## Context

Using a well-designed, well-documented and well-supported library saves time and
effort. Its quality and documentation are likely to be greater than what one
could produce if the majority of one's time must be spent on an implementation.
The cost of a library — time, effort, money — is shared over many users. A
widely used library is more likely to be kept up to date and ported to new
systems than an individual application, and knowledge of a widely used library
carries to other and future projects. So if a suitable library exists for the
application domain, use it.

```cpp
std::sort(begin(v), end(v), std::greater<>());
```

Unless one is an expert in sorting algorithms and has plenty of time, this is
more likely to be correct, and to run faster, than anything written for a
specific application.

The upstream framing of the burden of proof is the important part: a reason is
needed *not* to use the standard library, or whatever foundational libraries the
application uses — not a reason to use it.

By default, use the ISO C++ standard library and the Guidelines Support Library.
And if no well-designed, well-documented and well-supported library exists for an
important domain, maybe one should be designed, implemented, and then used.

## Decision

Use support libraries as appropriate, with the default being to use them.

- Reach for the standard library first, then whatever foundation libraries the
  project already depends on, before writing a mechanism by hand.
- Justify writing an equivalent by hand, not using the library. Acceptable
  reasons exist — a measured performance requirement the library cannot meet, a
  dependency that cannot be taken on, a domain the library does not actually
  cover — and they are stated, not assumed.
- Judge a candidate library on design, documentation and support, in that order,
  before convenience.
- Where an important domain has no suitable library, write the abstraction and
  use it, rather than open-coding the domain at each call site.

## Rationale

The economics are lopsided: a widely used library has had more expert attention,
more testing and more platforms than any local reimplementation will ever get,
and its maintenance is paid for by everyone who uses it. Against that, a local
version has to be written, debugged, documented and then maintained forever by
whoever is here.

Inverting the burden of proof is what makes the rule bite. "Use a library when
appropriate" is agreed to by everyone and changes nothing; "explain why you did
not" changes what gets written.

## Consequences

### Positive

- Less code to own, and what remains is closer to the problem than to the
  mechanism.
- Library code is typically better tested and faster than a local equivalent
  written under time pressure.
- Knowledge of a widely used library transfers, so the code is legible to
  someone seeing the project for the first time.

### Negative

- Every dependency is a commitment: build integration, an upgrade path, a
  licence, and a supply-chain surface. The library that is free to adopt is not
  free to keep.
- A library that is nearly right invites shaping the problem to fit the library,
  which is a subtler cost than writing the code would have been.
- Writing a new abstraction, in the no-suitable-library case, is exactly the
  work this rule was meant to avoid — now with the additional obligation to
  document and support it.

### Neutral

- Which specific libraries a project depends on, and how they are acquired and
  built, is a separate decision from this one.
- Adopting the GSL as a default is stated upstream; nothing in this record adds
  it to the build.
- Upstream states no enforcement for this rule, and none is mechanically
  feasible. It is applied at review, at the moment code that duplicates a
  library is proposed.

## Related

- [0007](0007-p-1-express-ideas-directly-in-code.md) — a library expresses intent
  better than direct use of language features.
- [0017](0017-p-11-encapsulate-messy-constructs-rather-than-spreading-through-the-code.md)
  — write the library when none exists.
