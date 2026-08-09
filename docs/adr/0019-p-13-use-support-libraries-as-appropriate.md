# 0019. P.13: Use support libraries as appropriate

Status: accepted
Date: 2026-08-09
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-09

## Context

The last philosophy rule, and the one this project has already answered six
times: ImGui, GLFW, libgit2, pugixml, nlohmann/json and doctest are all "use a
library rather than write one" decided in the library's favour.

It is also where the GSL question is settled for the whole directory. P.4, P.6,
P.8 and P.11 all name `gsl::span`, `gsl::owner` or `narrow_cast`, and this
repository has none of them.

## Decision

Adopt P.13 as written. Transcribed in full:

> **Reason.** Using a well-designed, well-documented, and well-supported library
> saves time and effort; its quality and documentation are likely to be greater
> than what you could do if the majority of your time must be spent on an
> implementation. The cost (time, effort, money, etc.) of a library can be
> shared over many users. A widely used library is more likely to be kept
> up-to-date and ported to new systems than an individual application. Knowledge
> of a widely-used library can save time on other/future projects. So, if a
> suitable library exists for your application domain, use it.

**Example.**

```cpp
std::sort(begin(v), end(v), std::greater<>());
```

Unless you are an expert in sorting algorithms and have plenty of time, this is
more likely to be correct and to run faster than anything you write for a
specific application. You need a reason not to use the standard library (or
whatever foundational libraries your application uses) rather than a reason to
use it.

**Note.** By default use the ISO C++ Standard Library and the Guidelines Support
Library.

**Note.** If no well-designed, well-documented, and well-supported library exists
for an important domain, maybe you should design and implement it, and then use
it.

## Rationale

Adopted as written for the standard library and for the six vendored
dependencies. The rule's framing — you need a reason *not* to use a library — is
the same test
[0003](0003-vendor-dependencies-as-git-submodules-built-by-premake.md) applies.

**The Guidelines Support Library is declined.** This is the one substantive
departure in the philosophy section, and it is deliberate:

- `gsl::span` is superseded by `std::span` in C++20, which this project already
  has by [0005](0005-target-c-20-with-headers-not-modules.md).
- `gsl::owner<T*>` is a marker with no runtime behaviour. Its value comes from a
  static analyser reading it, and there is no analyser here — see
  [0018](0018-p-12-use-supporting-tools-as-appropriate.md). Adding a dependency
  to satisfy a tool that does not exist is the wrong order.
- `narrow_cast` and `gsl::index` are small enough to write in `Core/Base` if
  they are ever wanted, without a seventh submodule and its premake script.

Wherever a transcribed rule in this directory names a GSL facility, read it as
the standard-library equivalent, or as "not available here" for `owner` and
`narrow_cast`.

Where the rule's second Note applies — no suitable library exists — this project
has written its own: the text buffer and the `.slnx` editing in `Core/Sln` and
`Core/Text` are domains where nothing off the shelf fit.

## Consequences

### Positive

- Settles the GSL question once, in one place, instead of leaving fifteen
  transcribed rules each pointing at a library the repository does not have.
- Matches what the project already does, so the rule costs nothing to adopt.

### Negative

- Declining the GSL means `narrow_cast` and `owner<T*>` have no substitute, so
  the hazards P.4 and P.8 name are unmarked in this codebase. That is a real
  cost of this decision, not a technicality.
- Six vendored libraries is itself a maintenance surface: each is a submodule to
  bump and, for four of them, a premake script to keep in step.

### Neutral

- The custom code the rule licenses — `TextBuffer`, `SlnxEditor` — is also the
  code with the heaviest test coverage in the repository, which is what makes
  writing it defensible.

## Deviations to fix

| What | Where | Note |
|---|---|---|
| `std::span` present in the precompiled header but never used, so the standard-library facility that replaces `gsl::span` is not actually adopted | `Core/src/corepch.h:13` | Declining the GSL only holds up if the standard replacement is used |

## Related

- [0018](0018-p-12-use-supporting-tools-as-appropriate.md) — the previous rule,
  and why `owner<T*>` would buy nothing today.
- [0003](0003-vendor-dependencies-as-git-submodules-built-by-premake.md) — how a
  library enters this repository.
- [0007](0007-p-1-express-ideas-directly-in-code.md) — the first philosophy rule.
