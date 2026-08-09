# Architecture decision records

Why this repository is the way it is — architecture and coding conventions
alike. Read [0001](0001-record-architecture-decisions.md) first: it explains
when a record is written, how one is structured, and how a decision is changed.

Create a record with the scaffolding script, which allocates the number and
adds the row below:

```powershell
.\scripts\New-Adr.ps1 "Require braces around every control-flow body"
```

Check every record's shape with:

```powershell
.\scripts\Test-Adr.ps1
```

| # | Title | Status | Date |
|---|---|---|---|
<!-- adr-index -->
| [0001](0001-record-architecture-decisions.md) | Record architecture decisions in this directory | accepted | 2026-08-09 |
| [0002](0002-keep-core-free-of-ide-knowledge.md) | Keep Core free of IDE knowledge | accepted | 2026-08-09 |
| [0003](0003-vendor-dependencies-as-git-submodules-built-by-premake.md) | Vendor dependencies as git submodules built by premake | accepted | 2026-08-09 |
| [0004](0004-return-result-t-instead-of-throwing-across-module-boundaries.md) | Return Result<T> instead of throwing across module boundaries | accepted | 2026-08-09 |
| [0005](0005-target-c-20-with-headers-not-modules.md) | Target C++20 with headers, not modules | accepted | 2026-08-09 |
| [0006](0006-require-braces-around-every-control-flow-body.md) | Require braces around every control-flow body | accepted | 2026-08-09 |
| [0007](0007-p-1-express-ideas-directly-in-code.md) | P.1: Express ideas directly in code | accepted | 2026-08-09 |
| [0008](0008-p-2-write-in-iso-standard-c.md) | P.2: Write in ISO Standard C++ | accepted | 2026-08-09 |
| [0009](0009-p-3-express-intent.md) | P.3: Express intent | accepted | 2026-08-09 |
| [0010](0010-p-4-ideally-a-program-should-be-statically-type-safe.md) | P.4: Ideally, a program should be statically type safe | accepted | 2026-08-09 |
| [0011](0011-p-5-prefer-compile-time-checking-to-run-time-checking.md) | P.5: Prefer compile-time checking to run-time checking | accepted | 2026-08-09 |
| [0012](0012-p-6-what-cannot-be-checked-at-compile-time-should-be-checkable-at-run-time.md) | P.6: What cannot be checked at compile time should be checkable at run time | accepted | 2026-08-09 |
| [0013](0013-p-7-catch-run-time-errors-early.md) | P.7: Catch run-time errors early | accepted | 2026-08-09 |
| [0014](0014-p-8-do-not-leak-any-resources.md) | P.8: Do not leak any resources | accepted | 2026-08-09 |
| [0015](0015-p-9-do-not-waste-time-or-space.md) | P.9: Do not waste time or space | accepted | 2026-08-09 |
| [0016](0016-p-10-prefer-immutable-data-to-mutable-data.md) | P.10: Prefer immutable data to mutable data | accepted | 2026-08-09 |
| [0017](0017-p-11-encapsulate-messy-constructs-rather-than-spreading-through-the-code.md) | P.11: Encapsulate messy constructs, rather than spreading through the code | accepted | 2026-08-09 |
| [0018](0018-p-12-use-supporting-tools-as-appropriate.md) | P.12: Use supporting tools as appropriate | accepted | 2026-08-09 |
| [0019](0019-p-13-use-support-libraries-as-appropriate.md) | P.13: Use support libraries as appropriate | accepted | 2026-08-09 |
| [0020](0020-i-1-make-interfaces-explicit.md) | I.1: Make interfaces explicit | accepted | 2026-08-09 |
| [0021](0021-i-2-avoid-non-const-global-variables.md) | I.2: Avoid non-const global variables | accepted | 2026-08-09 |
| [0022](0022-i-3-avoid-singletons.md) | I.3: Avoid singletons | accepted | 2026-08-09 |
| [0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md) | I.4: Make interfaces precisely and strongly typed | accepted | 2026-08-09 |
| [0024](0024-i-5-state-preconditions-if-any.md) | I.5: State preconditions (if any) | accepted | 2026-08-09 |
| [0025](0025-i-6-prefer-expects-for-expressing-preconditions.md) | I.6: Prefer Expects() for expressing preconditions | accepted | 2026-08-09 |
| [0026](0026-i-7-state-postconditions.md) | I.7: State postconditions | accepted | 2026-08-09 |
| [0027](0027-i-8-prefer-ensures-for-expressing-postconditions.md) | I.8: Prefer Ensures() for expressing postconditions | accepted | 2026-08-09 |
| [0028](0028-i-9-if-an-interface-is-a-template-document-its-parameters-using-concepts.md) | I.9: If an interface is a template, document its parameters using concepts | accepted | 2026-08-09 |
| [0029](0029-i-10-use-exceptions-to-signal-a-failure-to-perform-a-required-task.md) | I.10: Use exceptions to signal a failure to perform a required task | rejected | 2026-08-09 |
| [0030](0030-i-11-never-transfer-ownership-by-a-raw-pointer-or-reference.md) | I.11: Never transfer ownership by a raw pointer or reference | accepted | 2026-08-09 |
| [0031](0031-i-12-declare-a-pointer-that-must-not-be-null-as-not-null.md) | I.12: Declare a pointer that must not be null as not_null | accepted | 2026-08-09 |
| [0032](0032-i-13-do-not-pass-an-array-as-a-single-pointer.md) | I.13: Do not pass an array as a single pointer | accepted | 2026-08-09 |
| [0033](0033-i-22-avoid-complex-initialization-of-global-objects.md) | I.22: Avoid complex initialization of global objects | accepted | 2026-08-09 |
| [0034](0034-i-23-keep-the-number-of-function-arguments-low.md) | I.23: Keep the number of function arguments low | accepted | 2026-08-09 |
| [0035](0035-i-24-avoid-adjacent-parameters-that-can-be-invoked-by-the-same-arguments-in-either-order-with-different-meaning.md) | I.24: Avoid adjacent parameters that can be invoked by the same arguments in either order with different meaning | accepted | 2026-08-09 |
| [0036](0036-i-25-prefer-empty-abstract-classes-as-interfaces-to-class-hierarchies.md) | I.25: Prefer empty abstract classes as interfaces to class hierarchies | accepted | 2026-08-09 |
| [0037](0037-i-26-if-you-want-a-cross-compiler-abi-use-a-c-style-subset.md) | I.26: If you want a cross-compiler ABI, use a C-style subset | accepted | 2026-08-09 |
| [0038](0038-i-27-for-stable-library-abi-consider-the-pimpl-idiom.md) | I.27: For stable library ABI, consider the Pimpl idiom | accepted | 2026-08-09 |
| [0039](0039-i-30-encapsulate-rule-violations.md) | I.30: Encapsulate rule violations | accepted | 2026-08-09 |
<!-- /adr-index -->
