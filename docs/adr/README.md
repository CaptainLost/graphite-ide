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

The table below is rebuilt from the records themselves — their titles, statuses
and dates — so after promoting a record from `proposed`, or after any edit that
touches those three lines, run:

```powershell
.\scripts\Update-AdrIndex.ps1
```

Do not hand-edit the rows between the markers; edit the record and rebuild.

| # | Title | Status | Date |
|---|---|---|---|
<!-- adr-index -->
| [0001](0001-record-architecture-decisions.md) | Record architecture decisions in this directory | accepted | 2026-08-09 |
| [0002](0002-keep-core-free-of-ide-knowledge.md) | Keep Core free of IDE knowledge | accepted | 2026-08-09 |
| [0003](0003-vendor-dependencies-as-git-submodules-built-by-premake.md) | Vendor dependencies as git submodules built by premake | accepted | 2026-08-09 |
| [0004](0004-return-result-t-instead-of-throwing-across-module-boundaries.md) | Return Result<T> instead of throwing across module boundaries | accepted | 2026-08-09 |
| [0005](0005-target-c-20-with-headers-not-modules.md) | Target C++20 with headers, not modules | accepted | 2026-08-09 |
| [0006](0006-require-braces-around-every-control-flow-body.md) | Require braces around every control-flow body | accepted | 2026-08-09 |
| [0007](0007-p-1-express-ideas-directly-in-code.md) | P.1: Express ideas directly in code | accepted | 2026-08-10 |
| [0008](0008-p-2-write-in-iso-standard-c.md) | P.2: Write in ISO Standard C++ | accepted | 2026-08-10 |
| [0009](0009-p-3-express-intent.md) | P.3: Express intent | accepted | 2026-08-10 |
| [0010](0010-p-4-ideally-a-program-should-be-statically-type-safe.md) | P.4: Ideally, a program should be statically type safe | accepted | 2026-08-10 |
| [0011](0011-p-5-prefer-compile-time-checking-to-run-time-checking.md) | P.5: Prefer compile-time checking to run-time checking | accepted | 2026-08-10 |
| [0012](0012-p-6-what-cannot-be-checked-at-compile-time-should-be-checkable-at-run-time.md) | P.6: What cannot be checked at compile time should be checkable at run time | accepted | 2026-08-10 |
| [0013](0013-p-7-catch-run-time-errors-early.md) | P.7: Catch run-time errors early | accepted | 2026-08-10 |
| [0014](0014-p-8-do-not-leak-any-resources.md) | P.8: Do not leak any resources | accepted | 2026-08-10 |
| [0015](0015-p-9-do-not-waste-time-or-space.md) | P.9: Do not waste time or space | accepted | 2026-08-10 |
| [0016](0016-p-10-prefer-immutable-data-to-mutable-data.md) | P.10: Prefer immutable data to mutable data | accepted | 2026-08-10 |
| [0017](0017-p-11-encapsulate-messy-constructs-rather-than-spreading-through-the-code.md) | P.11: Encapsulate messy constructs, rather than spreading through the code | accepted | 2026-08-10 |
| [0018](0018-p-12-use-supporting-tools-as-appropriate.md) | P.12: Use supporting tools as appropriate | accepted | 2026-08-10 |
| [0019](0019-p-13-use-support-libraries-as-appropriate.md) | P.13: Use support libraries as appropriate | accepted | 2026-08-10 |
| [0020](0020-i-1-make-interfaces-explicit.md) | I.1: Make interfaces explicit | accepted | 2026-08-10 |
| [0021](0021-i-2-avoid-non-const-global-variables.md) | I.2: Avoid non-const global variables | accepted | 2026-08-10 |
| [0022](0022-i-3-avoid-singletons.md) | I.3: Avoid singletons | accepted | 2026-08-10 |
| [0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md) | I.4: Make interfaces precisely and strongly typed | accepted | 2026-08-10 |
| [0024](0024-i-5-state-preconditions-if-any.md) | I.5: State preconditions (if any) | accepted | 2026-08-10 |
| [0025](0025-i-6-prefer-expects-for-expressing-preconditions.md) | I.6: Prefer Expects() for expressing preconditions | accepted | 2026-08-10 |
| [0026](0026-i-7-state-postconditions.md) | I.7: State postconditions | accepted | 2026-08-10 |
| [0027](0027-i-8-prefer-ensures-for-expressing-postconditions.md) | I.8: Prefer Ensures() for expressing postconditions | accepted | 2026-08-10 |
| [0028](0028-i-9-if-an-interface-is-a-template-document-its-parameters-using-concepts.md) | I.9: If an interface is a template, document its parameters using concepts | accepted | 2026-08-10 |
| [0029](0029-i-10-use-exceptions-to-signal-a-failure-to-perform-a-required-task.md) | I.10: Use exceptions to signal a failure to perform a required task | accepted | 2026-08-10 |
| [0030](0030-i-11-never-transfer-ownership-by-a-raw-pointer-or-reference.md) | I.11: Never transfer ownership by a raw pointer (T*) or reference (T&) | accepted | 2026-08-10 |
| [0031](0031-i-12-declare-a-pointer-that-must-not-be-null-as-not-null.md) | I.12: Declare a pointer that must not be null as not_null | accepted | 2026-08-10 |
| [0032](0032-i-13-do-not-pass-an-array-as-a-single-pointer.md) | I.13: Do not pass an array as a single pointer | accepted | 2026-08-10 |
| [0033](0033-i-22-avoid-complex-initialization-of-global-objects.md) | I.22: Avoid complex initialization of global objects | accepted | 2026-08-10 |
| [0034](0034-i-23-keep-the-number-of-function-arguments-low.md) | I.23: Keep the number of function arguments low | accepted | 2026-08-10 |
| [0035](0035-i-24-avoid-adjacent-parameters-that-can-be-invoked-by-the-same-arguments-in-either-order-with-different-meaning.md) | I.24: Avoid adjacent parameters that can be invoked by the same arguments in either order with different meaning | accepted | 2026-08-10 |
| [0036](0036-i-25-prefer-empty-abstract-classes-as-interfaces-to-class-hierarchies.md) | I.25: Prefer empty abstract classes as interfaces to class hierarchies | accepted | 2026-08-10 |
| [0037](0037-i-26-if-you-want-a-cross-compiler-abi-use-a-c-style-subset.md) | I.26: If you want a cross-compiler ABI, use a C-style subset | accepted | 2026-08-10 |
| [0038](0038-i-27-for-stable-library-abi-consider-the-pimpl-idiom.md) | I.27: For stable library ABI, consider the Pimpl idiom | accepted | 2026-08-10 |
| [0039](0039-i-30-encapsulate-rule-violations.md) | I.30: Encapsulate rule violations | accepted | 2026-08-10 |
| [0040](0040-c-1-organize-related-data-into-structures-structs-or-classes.md) | C.1: Organize related data into structures (structs or classes) | accepted | 2026-08-10 |
| [0041](0041-c-2-use-class-if-the-class-has-an-invariant-use-struct-if-the-data-members-can-vary-independently.md) | C.2: Use class if the class has an invariant; use struct if the data members can vary independently | accepted | 2026-08-10 |
| [0042](0042-c-3-represent-the-distinction-between-an-interface-and-an-implementation-using-a-class.md) | C.3: Represent the distinction between an interface and an implementation using a class | accepted | 2026-08-10 |
| [0043](0043-c-4-make-a-function-a-member-only-if-it-needs-direct-access-to-the-representation-of-a-class.md) | C.4: Make a function a member only if it needs direct access to the representation of a class | accepted | 2026-08-10 |
| [0044](0044-c-5-place-helper-functions-in-the-same-namespace-as-the-class-they-support.md) | C.5: Place helper functions in the same namespace as the class they support | accepted | 2026-08-10 |
| [0045](0045-c-7-dont-define-a-class-or-enum-and-declare-a-variable-of-its-type-in-the-same-statement.md) | C.7: Don't define a class or enum and declare a variable of its type in the same statement | accepted | 2026-08-10 |
| [0046](0046-c-8-use-class-rather-than-struct-if-any-member-is-non-public.md) | C.8: Use class rather than struct if any member is non-public | accepted | 2026-08-10 |
| [0047](0047-c-9-minimize-exposure-of-members.md) | C.9: Minimize exposure of members | accepted | 2026-08-10 |
| [0048](0048-c-10-prefer-concrete-types-over-class-hierarchies.md) | C.10: Prefer concrete types over class hierarchies | accepted | 2026-08-10 |
| [0049](0049-c-11-make-concrete-types-regular.md) | C.11: Make concrete types regular | accepted | 2026-08-10 |
| [0050](0050-c-12-dont-make-data-members-const-or-references.md) | C.12: Don't make data members const or references | accepted | 2026-08-10 |
| [0051](0051-c-20-if-you-can-avoid-defining-default-operations-do.md) | C.20: If you can avoid defining default operations, do | accepted | 2026-08-10 |
| [0052](0052-c-21-if-you-define-or-delete-any-copy-move-or-destructor-function-define-or-delete-them-all.md) | C.21: If you define or =delete any copy, move, or destructor function, define or =delete them all | accepted | 2026-08-10 |
| [0053](0053-c-22-make-default-operations-consistent.md) | C.22: Make default operations consistent | accepted | 2026-08-10 |
| [0054](0054-c-30-define-a-destructor-if-a-class-needs-an-explicit-action-at-object-destruction.md) | C.30: Define a destructor if a class needs an explicit action at object destruction | accepted | 2026-08-10 |
| [0055](0055-c-31-all-resources-acquired-by-a-class-must-be-released-by-the-classs-destructor.md) | C.31: All resources acquired by a class must be released by the class's destructor | accepted | 2026-08-10 |
| [0056](0056-c-32-if-a-class-has-a-raw-pointer-t-or-reference-t-consider-whether-it-might-be-owning.md) | C.32: If a class has a raw pointer (T*) or reference (T&), consider whether it might be owning | accepted | 2026-08-10 |
| [0057](0057-c-33-if-a-class-has-an-owning-pointer-member-define-a-destructor.md) | C.33: If a class has an owning pointer member, define a destructor | accepted | 2026-08-10 |
| [0058](0058-c-35-a-base-class-destructor-should-be-either-public-and-virtual-or-protected-and-non-virtual.md) | C.35: A base class destructor should be either public and virtual, or protected and non-virtual | accepted | 2026-08-10 |
| [0059](0059-c-36-a-destructor-must-not-fail.md) | C.36: A destructor must not fail | accepted | 2026-08-10 |
| [0060](0060-c-37-make-destructors-noexcept.md) | C.37: Make destructors noexcept | accepted | 2026-08-10 |
| [0061](0061-c-40-define-a-constructor-if-a-class-has-an-invariant.md) | C.40: Define a constructor if a class has an invariant | accepted | 2026-08-10 |
| [0062](0062-c-41-a-constructor-should-create-a-fully-initialized-object.md) | C.41: A constructor should create a fully initialized object | accepted | 2026-08-10 |
| [0063](0063-c-42-if-a-constructor-cannot-construct-a-valid-object-throw-an-exception.md) | C.42: If a constructor cannot construct a valid object, throw an exception | accepted | 2026-08-10 |
| [0064](0064-c-43-ensure-that-a-copyable-class-has-a-default-constructor.md) | C.43: Ensure that a copyable class has a default constructor | accepted | 2026-08-10 |
| [0065](0065-c-44-prefer-default-constructors-to-be-simple-and-non-throwing.md) | C.44: Prefer default constructors to be simple and non-throwing | accepted | 2026-08-10 |
| [0066](0066-c-45-dont-define-a-default-constructor-that-only-initializes-data-members-use-in-class-member-initializers-instead.md) | C.45: Don't define a default constructor that only initializes data members; use in-class member initializers instead | accepted | 2026-08-10 |
| [0067](0067-c-46-by-default-declare-single-argument-constructors-explicit.md) | C.46: By default, declare single-argument constructors explicit | accepted | 2026-08-10 |
| [0068](0068-c-47-define-and-initialize-member-variables-in-the-order-of-member-declaration.md) | C.47: Define and initialize member variables in the order of member declaration | accepted | 2026-08-10 |
| [0069](0069-c-48-prefer-in-class-initializers-to-member-initializers-in-constructors-for-constant-initializers.md) | C.48: Prefer in-class initializers to member initializers in constructors for constant initializers | accepted | 2026-08-10 |
| [0070](0070-c-49-prefer-initialization-to-assignment-in-constructors.md) | C.49: Prefer initialization to assignment in constructors | accepted | 2026-08-10 |
| [0071](0071-c-50-use-a-factory-function-if-you-need-virtual-behavior-during-initialization.md) | C.50: Use a factory function if you need "virtual behavior" during initialization | accepted | 2026-08-10 |
| [0072](0072-c-51-use-delegating-constructors-to-represent-common-actions-for-all-constructors-of-a-class.md) | C.51: Use delegating constructors to represent common actions for all constructors of a class | accepted | 2026-08-10 |
| [0073](0073-c-52-use-inheriting-constructors-to-import-constructors-into-a-derived-class-that-does-not-need-further-explicit-initialization.md) | C.52: Use inheriting constructors to import constructors into a derived class that does not need further explicit initialization | accepted | 2026-08-10 |
| [0074](0074-c-60-make-copy-assignment-non-virtual-take-the-parameter-by-const-and-return-by-non-const.md) | C.60: Make copy assignment non-virtual, take the parameter by const&, and return by non-const& | accepted | 2026-08-10 |
| [0075](0075-c-61-a-copy-operation-should-copy.md) | C.61: A copy operation should copy | accepted | 2026-08-10 |
| [0076](0076-c-62-make-copy-assignment-safe-for-self-assignment.md) | C.62: Make copy assignment safe for self-assignment | accepted | 2026-08-10 |
| [0077](0077-c-63-make-move-assignment-non-virtual-take-the-parameter-by-and-return-by-non-const.md) | C.63: Make move assignment non-virtual, take the parameter by &&, and return by non-const& | accepted | 2026-08-10 |
| [0078](0078-c-64-a-move-operation-should-move-and-leave-its-source-in-a-valid-state.md) | C.64: A move operation should move and leave its source in a valid state | accepted | 2026-08-10 |
| [0079](0079-c-65-make-move-assignment-safe-for-self-assignment.md) | C.65: Make move assignment safe for self-assignment | accepted | 2026-08-10 |
| [0080](0080-c-66-make-move-operations-noexcept.md) | C.66: Make move operations noexcept | accepted | 2026-08-10 |
| [0081](0081-c-67-a-polymorphic-class-should-suppress-public-copy-move.md) | C.67: A polymorphic class should suppress public copy/move | accepted | 2026-08-10 |
| [0082](0082-c-80-use-default-if-you-have-to-be-explicit-about-using-the-default-semantics.md) | C.80: Use =default if you have to be explicit about using the default semantics | accepted | 2026-08-10 |
| [0083](0083-c-81-use-delete-when-you-want-to-disable-default-behavior-without-wanting-an-alternative.md) | C.81: Use =delete when you want to disable default behavior (without wanting an alternative) | accepted | 2026-08-10 |
| [0084](0084-c-82-dont-call-virtual-functions-in-constructors-and-destructors.md) | C.82: Don't call virtual functions in constructors and destructors | accepted | 2026-08-10 |
| [0085](0085-c-83-for-value-like-types-consider-providing-a-noexcept-swap-function.md) | C.83: For value-like types, consider providing a noexcept swap function | accepted | 2026-08-10 |
| [0086](0086-c-84-a-swap-function-must-not-fail.md) | C.84: A swap function must not fail | accepted | 2026-08-10 |
| [0087](0087-c-85-make-swap-noexcept.md) | C.85: Make swap noexcept | accepted | 2026-08-10 |
| [0088](0088-c-86-make-symmetric-with-respect-to-operand-types-and-noexcept.md) | C.86: Make == symmetric with respect to operand types and noexcept | accepted | 2026-08-10 |
| [0089](0089-c-87-beware-of-on-base-classes.md) | C.87: Beware of == on base classes | accepted | 2026-08-10 |
| [0090](0090-c-89-make-a-hash-noexcept.md) | C.89: Make a hash noexcept | accepted | 2026-08-10 |
| [0091](0091-c-90-rely-on-constructors-and-assignment-operators-not-memset-and-memcpy.md) | C.90: Rely on constructors and assignment operators, not memset and memcpy | accepted | 2026-08-10 |
| [0092](0092-c-100-follow-the-stl-when-defining-a-container.md) | C.100: Follow the STL when defining a container | accepted | 2026-08-10 |
| [0093](0093-c-101-give-a-container-value-semantics.md) | C.101: Give a container value semantics | accepted | 2026-08-10 |
| [0094](0094-c-102-give-a-container-move-operations.md) | C.102: Give a container move operations | accepted | 2026-08-10 |
| [0095](0095-c-103-give-a-container-an-initializer-list-constructor.md) | C.103: Give a container an initializer list constructor | accepted | 2026-08-10 |
| [0096](0096-c-104-give-a-container-a-default-constructor-that-sets-it-to-empty.md) | C.104: Give a container a default constructor that sets it to empty | accepted | 2026-08-10 |
| [0097](0097-c-109-if-a-resource-handle-has-pointer-semantics-provide-and.md) | C.109: If a resource handle has pointer semantics, provide * and -> | accepted | 2026-08-10 |
| [0098](0098-c-120-use-class-hierarchies-to-represent-concepts-with-inherent-hierarchical-structure-only.md) | C.120: Use class hierarchies to represent concepts with inherent hierarchical structure (only) | accepted | 2026-08-10 |
| [0099](0099-c-121-if-a-base-class-is-used-as-an-interface-make-it-a-pure-abstract-class.md) | C.121: If a base class is used as an interface, make it a pure abstract class | accepted | 2026-08-10 |
| [0100](0100-c-122-use-abstract-classes-as-interfaces-when-complete-separation-of-interface-and-implementation-is-needed.md) | C.122: Use abstract classes as interfaces when complete separation of interface and implementation is needed | accepted | 2026-08-10 |
| [0101](0101-c-126-an-abstract-class-typically-doesnt-need-a-user-written-constructor.md) | C.126: An abstract class typically doesn't need a user-written constructor | accepted | 2026-08-10 |
| [0102](0102-c-127-a-class-with-a-virtual-function-should-have-a-virtual-or-protected-destructor.md) | C.127: A class with a virtual function should have a virtual or protected destructor | accepted | 2026-08-10 |
| [0103](0103-c-128-virtual-functions-should-specify-exactly-one-of-virtual-override-or-final.md) | C.128: Virtual functions should specify exactly one of virtual, override, or final | accepted | 2026-08-10 |
| [0104](0104-c-129-when-designing-a-class-hierarchy-distinguish-between-implementation-inheritance-and-interface-inheritance.md) | C.129: When designing a class hierarchy, distinguish between implementation inheritance and interface inheritance | accepted | 2026-08-10 |
| [0105](0105-c-130-for-making-deep-copies-of-polymorphic-classes-prefer-a-virtual-clone-function-instead-of-public-copy-construction-assignment.md) | C.130: For making deep copies of polymorphic classes prefer a virtual clone function instead of public copy construction/assignment | accepted | 2026-08-10 |
| [0106](0106-c-131-avoid-trivial-getters-and-setters.md) | C.131: Avoid trivial getters and setters | accepted | 2026-08-10 |
| [0107](0107-c-132-dont-make-a-function-virtual-without-reason.md) | C.132: Don't make a function virtual without reason | accepted | 2026-08-10 |
| [0108](0108-c-133-avoid-protected-data.md) | C.133: Avoid protected data | accepted | 2026-08-10 |
| [0109](0109-c-134-ensure-all-non-const-data-members-have-the-same-access-level.md) | C.134: Ensure all non-const data members have the same access level | accepted | 2026-08-10 |
| [0110](0110-c-135-use-multiple-inheritance-to-represent-multiple-distinct-interfaces.md) | C.135: Use multiple inheritance to represent multiple distinct interfaces | accepted | 2026-08-10 |
| [0111](0111-c-136-use-multiple-inheritance-to-represent-the-union-of-implementation-attributes.md) | C.136: Use multiple inheritance to represent the union of implementation attributes | accepted | 2026-08-10 |
| [0112](0112-c-137-use-virtual-bases-to-avoid-overly-general-base-classes.md) | C.137: Use virtual bases to avoid overly general base classes | accepted | 2026-08-10 |
| [0113](0113-c-138-create-an-overload-set-for-a-derived-class-and-its-bases-with-using.md) | C.138: Create an overload set for a derived class and its bases with using | accepted | 2026-08-10 |
| [0114](0114-c-139-use-final-on-classes-sparingly.md) | C.139: Use final on classes sparingly | accepted | 2026-08-10 |
| [0115](0115-c-140-do-not-provide-different-default-arguments-for-a-virtual-function-and-an-overrider.md) | C.140: Do not provide different default arguments for a virtual function and an overrider | accepted | 2026-08-10 |
| [0116](0116-c-145-access-polymorphic-objects-through-pointers-and-references.md) | C.145: Access polymorphic objects through pointers and references | accepted | 2026-08-10 |
| [0117](0117-c-146-use-dynamic-cast-where-class-hierarchy-navigation-is-unavoidable.md) | C.146: Use dynamic_cast where class hierarchy navigation is unavoidable | accepted | 2026-08-10 |
| [0118](0118-c-147-use-dynamic-cast-to-a-reference-type-when-failure-to-find-the-required-class-is-considered-an-error.md) | C.147: Use dynamic_cast to a reference type when failure to find the required class is considered an error | accepted | 2026-08-10 |
| [0119](0119-c-148-use-dynamic-cast-to-a-pointer-type-when-failure-to-find-the-required-class-is-considered-a-valid-alternative.md) | C.148: Use dynamic_cast to a pointer type when failure to find the required class is considered a valid alternative | accepted | 2026-08-10 |
| [0120](0120-c-149-use-unique-ptr-or-shared-ptr-to-avoid-forgetting-to-delete-objects-created-using-new.md) | C.149: Use unique_ptr or shared_ptr to avoid forgetting to delete objects created using new | accepted | 2026-08-10 |
| [0121](0121-c-150-use-make-unique-to-construct-objects-owned-by-unique-ptrs.md) | C.150: Use make_unique() to construct objects owned by unique_ptrs | accepted | 2026-08-10 |
| [0122](0122-c-151-use-make-shared-to-construct-objects-owned-by-shared-ptrs.md) | C.151: Use make_shared() to construct objects owned by shared_ptrs | accepted | 2026-08-10 |
| [0123](0123-c-152-never-assign-a-pointer-to-an-array-of-derived-class-objects-to-a-pointer-to-its-base.md) | C.152: Never assign a pointer to an array of derived class objects to a pointer to its base | accepted | 2026-08-10 |
| [0124](0124-c-153-prefer-virtual-function-to-casting.md) | C.153: Prefer virtual function to casting | accepted | 2026-08-10 |
| [0125](0125-c-160-define-operators-primarily-to-mimic-conventional-usage.md) | C.160: Define operators primarily to mimic conventional usage | accepted | 2026-08-10 |
| [0126](0126-c-161-use-non-member-functions-for-symmetric-operators.md) | C.161: Use non-member functions for symmetric operators | accepted | 2026-08-10 |
| [0127](0127-c-162-overload-operations-that-are-roughly-equivalent.md) | C.162: Overload operations that are roughly equivalent | accepted | 2026-08-10 |
| [0128](0128-c-163-overload-only-for-operations-that-are-roughly-equivalent.md) | C.163: Overload only for operations that are roughly equivalent | accepted | 2026-08-10 |
| [0129](0129-c-164-avoid-implicit-conversion-operators.md) | C.164: Avoid implicit conversion operators | accepted | 2026-08-10 |
| [0130](0130-c-165-use-using-for-customization-points.md) | C.165: Use using for customization points | accepted | 2026-08-10 |
| [0131](0131-c-166-overload-unary-only-as-part-of-a-system-of-smart-pointers-and-references.md) | C.166: Overload unary & only as part of a system of smart pointers and references | accepted | 2026-08-10 |
| [0132](0132-c-167-use-an-operator-for-an-operation-with-its-conventional-meaning.md) | C.167: Use an operator for an operation with its conventional meaning | accepted | 2026-08-10 |
| [0133](0133-c-168-define-overloaded-operators-in-the-namespace-of-their-operands.md) | C.168: Define overloaded operators in the namespace of their operands | accepted | 2026-08-10 |
| [0134](0134-c-170-if-you-feel-like-overloading-a-lambda-use-a-generic-lambda.md) | C.170: If you feel like overloading a lambda, use a generic lambda | accepted | 2026-08-10 |
| [0135](0135-c-180-use-unions-to-save-memory.md) | C.180: Use unions to save memory | accepted | 2026-08-10 |
| [0136](0136-c-181-avoid-naked-unions.md) | C.181: Avoid "naked" unions | accepted | 2026-08-10 |
| [0137](0137-c-182-use-anonymous-unions-to-implement-tagged-unions.md) | C.182: Use anonymous unions to implement tagged unions | accepted | 2026-08-10 |
| [0138](0138-c-183-dont-use-a-union-for-type-punning.md) | C.183: Don't use a union for type punning | accepted | 2026-08-10 |
| [0139](0139-enum-1-prefer-enumerations-over-macros.md) | Enum.1: Prefer enumerations over macros | accepted | 2026-08-10 |
| [0140](0140-enum-2-use-enumerations-to-represent-sets-of-related-named-constants.md) | Enum.2: Use enumerations to represent sets of related named constants | accepted | 2026-08-10 |
| [0141](0141-enum-3-prefer-class-enums-over-plain-enums.md) | Enum.3: Prefer class enums over "plain" enums | accepted | 2026-08-10 |
| [0142](0142-enum-4-define-operations-on-enumerations-for-safe-and-simple-use.md) | Enum.4: Define operations on enumerations for safe and simple use | accepted | 2026-08-10 |
| [0143](0143-enum-5-dont-use-all-caps-for-enumerators.md) | Enum.5: Don't use ALL_CAPS for enumerators | accepted | 2026-08-10 |
| [0144](0144-enum-6-avoid-unnamed-enumerations.md) | Enum.6: Avoid unnamed enumerations | accepted | 2026-08-10 |
| [0145](0145-enum-7-specify-the-underlying-type-of-an-enumeration-only-when-necessary.md) | Enum.7: Specify the underlying type of an enumeration only when necessary | accepted | 2026-08-10 |
| [0146](0146-enum-8-specify-enumerator-values-only-when-necessary.md) | Enum.8: Specify enumerator values only when necessary | accepted | 2026-08-10 |
| [0147](0147-r-1-manage-resources-automatically-using-resource-handles-and-raii-resource-acquisition-is-initialization.md) | R.1: Manage resources automatically using resource handles and RAII (Resource Acquisition Is Initialization) | accepted | 2026-08-10 |
| [0148](0148-r-2-in-interfaces-use-raw-pointers-to-denote-individual-objects-only.md) | R.2: In interfaces, use raw pointers to denote individual objects (only) | accepted | 2026-08-10 |
| [0149](0149-r-3-a-raw-pointer-a-t-is-non-owning.md) | R.3: A raw pointer (a T*) is non-owning | accepted | 2026-08-10 |
| [0150](0150-r-4-a-raw-reference-a-t-is-non-owning.md) | R.4: A raw reference (a T&) is non-owning | accepted | 2026-08-10 |
| [0151](0151-r-5-prefer-scoped-objects-dont-heap-allocate-unnecessarily.md) | R.5: Prefer scoped objects, don't heap-allocate unnecessarily | accepted | 2026-08-10 |
| [0152](0152-r-6-avoid-non-const-global-variables.md) | R.6: Avoid non-const global variables | accepted | 2026-08-10 |
| [0153](0153-r-10-avoid-malloc-and-free.md) | R.10: Avoid malloc() and free() | accepted | 2026-08-10 |
| [0154](0154-r-11-avoid-calling-new-and-delete-explicitly.md) | R.11: Avoid calling new and delete explicitly | accepted | 2026-08-10 |
| [0155](0155-r-12-immediately-give-the-result-of-an-explicit-resource-allocation-to-a-manager-object.md) | R.12: Immediately give the result of an explicit resource allocation to a manager object | accepted | 2026-08-10 |
| [0156](0156-r-13-perform-at-most-one-explicit-resource-allocation-in-a-single-expression-statement.md) | R.13: Perform at most one explicit resource allocation in a single expression statement | accepted | 2026-08-10 |
| [0157](0157-r-14-avoid-parameters-prefer-span.md) | R.14: Avoid [] parameters, prefer span | accepted | 2026-08-10 |
| [0158](0158-r-15-always-overload-matched-allocation-deallocation-pairs.md) | R.15: Always overload matched allocation/deallocation pairs | accepted | 2026-08-10 |
| [0159](0159-r-20-use-unique-ptr-or-shared-ptr-to-represent-ownership.md) | R.20: Use unique_ptr or shared_ptr to represent ownership | accepted | 2026-08-10 |
| [0160](0160-r-21-prefer-unique-ptr-over-shared-ptr-unless-you-need-to-share-ownership.md) | R.21: Prefer unique_ptr over shared_ptr unless you need to share ownership | accepted | 2026-08-10 |
| [0161](0161-r-22-use-make-shared-to-make-shared-ptrs.md) | R.22: Use make_shared() to make shared_ptrs | accepted | 2026-08-10 |
| [0162](0162-r-23-use-make-unique-to-make-unique-ptrs.md) | R.23: Use make_unique() to make unique_ptrs | accepted | 2026-08-10 |
| [0163](0163-r-24-use-std-weak-ptr-to-break-cycles-of-shared-ptrs.md) | R.24: Use std::weak_ptr to break cycles of shared_ptrs | accepted | 2026-08-10 |
| [0164](0164-r-30-take-smart-pointers-as-parameters-only-to-explicitly-express-lifetime-semantics.md) | R.30: Take smart pointers as parameters only to explicitly express lifetime semantics | accepted | 2026-08-10 |
| [0165](0165-r-31-if-you-have-non-std-smart-pointers-follow-the-basic-pattern-from-std.md) | R.31: If you have non-std smart pointers, follow the basic pattern from std | accepted | 2026-08-10 |
| [0166](0166-r-32-take-a-unique-ptr-widget-parameter-to-express-that-a-function-assumes-ownership-of-a-widget.md) | R.32: Take a unique_ptr<widget> parameter to express that a function assumes ownership of a widget | accepted | 2026-08-10 |
| [0167](0167-r-33-take-a-unique-ptr-widget-parameter-to-express-that-a-function-reseats-thewidget.md) | R.33: Take a unique_ptr<widget>& parameter to express that a function reseats the widget | accepted | 2026-08-10 |
| [0168](0168-r-34-take-a-shared-ptr-widget-parameter-to-express-shared-ownership.md) | R.34: Take a shared_ptr<widget> parameter to express shared ownership | accepted | 2026-08-10 |
| [0169](0169-r-35-take-a-shared-ptr-widget-parameter-to-express-that-a-function-might-reseat-the-shared-pointer.md) | R.35: Take a shared_ptr<widget>& parameter to express that a function might reseat the shared pointer | accepted | 2026-08-10 |
| [0170](0170-r-36-take-a-const-shared-ptr-widget-parameter-to-express-that-it-might-retain-a-reference-count-to-the-object.md) | R.36: Take a const shared_ptr<widget>& parameter to express that it might retain a reference count to the object | accepted | 2026-08-10 |
| [0171](0171-r-37-do-not-pass-a-pointer-or-reference-obtained-from-an-aliased-smart-pointer.md) | R.37: Do not pass a pointer or reference obtained from an aliased smart pointer | accepted | 2026-08-10 |
| [0172](0172-es-1-prefer-the-standard-library-to-other-libraries-and-to-handcrafted-code.md) | ES.1: Prefer the standard library to other libraries and to "handcrafted code" | accepted | 2026-08-10 |
| [0173](0173-es-2-prefer-suitable-abstractions-to-direct-use-of-language-features.md) | ES.2: Prefer suitable abstractions to direct use of language features | accepted | 2026-08-10 |
| [0174](0174-es-3-dont-repeat-yourself-avoid-redundant-code.md) | ES.3: Don't repeat yourself, avoid redundant code | accepted | 2026-08-10 |
| [0175](0175-es-5-keep-scopes-small.md) | ES.5: Keep scopes small | accepted | 2026-08-10 |
| [0176](0176-es-6-declare-names-in-for-statement-initializers-and-conditions-to-limit-scope.md) | ES.6: Declare names in for-statement initializers and conditions to limit scope | accepted | 2026-08-10 |
| [0177](0177-es-7-keep-common-and-local-names-short-and-keep-uncommon-and-non-local-names-longer.md) | ES.7: Keep common and local names short, and keep uncommon and non-local names longer | superseded by 0559 | 2026-08-10 |
| [0178](0178-es-8-avoid-similar-looking-names.md) | ES.8: Avoid similar-looking names | accepted | 2026-08-10 |
| [0179](0179-es-9-avoid-all-caps-names.md) | ES.9: Avoid ALL_CAPS names | accepted | 2026-08-10 |
| [0180](0180-es-10-declare-one-name-only-per-declaration.md) | ES.10: Declare one name (only) per declaration | accepted | 2026-08-10 |
| [0181](0181-es-11-use-auto-to-avoid-redundant-repetition-of-type-names.md) | ES.11: Use auto to avoid redundant repetition of type names | superseded by 0560 | 2026-08-10 |
| [0182](0182-es-12-do-not-reuse-names-in-nested-scopes.md) | ES.12: Do not reuse names in nested scopes | accepted | 2026-08-10 |
| [0183](0183-es-20-always-initialize-an-object.md) | ES.20: Always initialize an object | accepted | 2026-08-10 |
| [0184](0184-es-21-dont-introduce-a-variable-or-constant-before-you-need-to-use-it.md) | ES.21: Don't introduce a variable (or constant) before you need to use it | accepted | 2026-08-10 |
| [0185](0185-es-22-dont-declare-a-variable-until-you-have-a-value-to-initialize-it-with.md) | ES.22: Don't declare a variable until you have a value to initialize it with | accepted | 2026-08-10 |
| [0186](0186-es-23-prefer-the-initializer-syntax.md) | ES.23: Prefer the {}-initializer syntax | accepted | 2026-08-10 |
| [0187](0187-es-24-use-a-unique-ptr-t-to-hold-pointers.md) | ES.24: Use a unique_ptr<T> to hold pointers | accepted | 2026-08-10 |
| [0188](0188-es-25-declare-an-object-const-or-constexpr-unless-you-want-to-modify-its-value-later-on.md) | ES.25: Declare an object const or constexpr unless you want to modify its value later on | accepted | 2026-08-10 |
| [0189](0189-es-26-dont-use-a-variable-for-two-unrelated-purposes.md) | ES.26: Don't use a variable for two unrelated purposes | accepted | 2026-08-10 |
| [0190](0190-es-27-use-std-array-or-stack-array-for-arrays-on-the-stack.md) | ES.27: Use std::array or stack_array for arrays on the stack | accepted | 2026-08-10 |
| [0191](0191-es-28-use-lambdas-for-complex-initialization-especially-of-const-variables.md) | ES.28: Use lambdas for complex initialization, especially of const variables | superseded by 0561 | 2026-08-10 |
| [0192](0192-es-30-dont-use-macros-for-program-text-manipulation.md) | ES.30: Don't use macros for program text manipulation | accepted | 2026-08-10 |
| [0193](0193-es-31-dont-use-macros-for-constants-or-functions.md) | ES.31: Don't use macros for constants or "functions" | accepted | 2026-08-10 |
| [0194](0194-es-32-use-all-caps-for-all-macro-names.md) | ES.32: Use ALL_CAPS for all macro names | accepted | 2026-08-10 |
| [0195](0195-es-33-if-you-must-use-macros-give-them-unique-names.md) | ES.33: If you must use macros, give them unique names | accepted | 2026-08-10 |
| [0196](0196-es-34-dont-define-a-c-style-variadic-function.md) | ES.34: Don't define a (C-style) variadic function | accepted | 2026-08-10 |
| [0197](0197-es-40-avoid-complicated-expressions.md) | ES.40: Avoid complicated expressions | accepted | 2026-08-10 |
| [0198](0198-es-41-if-in-doubt-about-operator-precedence-parenthesize.md) | ES.41: If in doubt about operator precedence, parenthesize | accepted | 2026-08-10 |
| [0199](0199-es-42-keep-use-of-pointers-simple-and-straightforward.md) | ES.42: Keep use of pointers simple and straightforward | accepted | 2026-08-10 |
| [0200](0200-es-43-avoid-expressions-with-undefined-order-of-evaluation.md) | ES.43: Avoid expressions with undefined order of evaluation | accepted | 2026-08-10 |
| [0201](0201-es-44-dont-depend-on-order-of-evaluation-of-function-arguments.md) | ES.44: Don't depend on order of evaluation of function arguments | accepted | 2026-08-10 |
| [0202](0202-es-45-avoid-magic-constants-use-symbolic-constants.md) | ES.45: Avoid "magic constants"; use symbolic constants | accepted | 2026-08-10 |
| [0203](0203-es-46-avoid-lossy-narrowing-truncating-arithmetic-conversions.md) | ES.46: Avoid lossy (narrowing, truncating) arithmetic conversions | accepted | 2026-08-10 |
| [0204](0204-es-47-use-nullptr-rather-than-0-or-null.md) | ES.47: Use nullptr rather than 0 or NULL | accepted | 2026-08-10 |
| [0205](0205-es-48-avoid-casts.md) | ES.48: Avoid casts | accepted | 2026-08-10 |
| [0206](0206-es-49-if-you-must-use-a-cast-use-a-named-cast.md) | ES.49: If you must use a cast, use a named cast | accepted | 2026-08-10 |
| [0207](0207-es-50-dont-cast-away-const.md) | ES.50: Don't cast away const | accepted | 2026-08-10 |
| [0208](0208-es-55-avoid-the-need-for-range-checking.md) | ES.55: Avoid the need for range checking | accepted | 2026-08-10 |
| [0209](0209-es-56-write-std-move-only-when-you-need-to-explicitly-move-an-object-to-another-scope.md) | ES.56: Write std::move() only when you need to explicitly move an object to another scope | accepted | 2026-08-10 |
| [0210](0210-es-60-avoid-new-and-delete-outside-resource-management-functions.md) | ES.60: Avoid new and delete outside resource management functions | accepted | 2026-08-10 |
| [0211](0211-es-61-delete-arrays-using-delete-and-non-arrays-using-delete.md) | ES.61: Delete arrays using delete[] and non-arrays using delete | accepted | 2026-08-10 |
| [0212](0212-es-62-dont-compare-pointers-into-different-arrays.md) | ES.62: Don't compare pointers into different arrays | accepted | 2026-08-10 |
| [0213](0213-es-63-dont-slice.md) | ES.63: Don't slice | accepted | 2026-08-10 |
| [0214](0214-es-64-use-the-t-e-notation-for-construction.md) | ES.64: Use the T{e} notation for construction | accepted | 2026-08-10 |
| [0215](0215-es-65-dont-dereference-an-invalid-pointer.md) | ES.65: Don't dereference an invalid pointer | accepted | 2026-08-10 |
| [0216](0216-es-70-prefer-a-switch-statement-to-an-if-statement-when-there-is-a-choice.md) | ES.70: Prefer a switch-statement to an if-statement when there is a choice | accepted | 2026-08-10 |
| [0217](0217-es-71-prefer-a-range-for-statement-to-a-for-statement-when-there-is-a-choice.md) | ES.71: Prefer a range-for-statement to a for-statement when there is a choice | accepted | 2026-08-10 |
| [0218](0218-es-72-prefer-a-for-statement-to-a-while-statement-when-there-is-an-obvious-loop-variable.md) | ES.72: Prefer a for-statement to a while-statement when there is an obvious loop variable | accepted | 2026-08-10 |
| [0219](0219-es-73-prefer-a-while-statement-to-a-for-statement-when-there-is-no-obvious-loop-variable.md) | ES.73: Prefer a while-statement to a for-statement when there is no obvious loop variable | accepted | 2026-08-10 |
| [0220](0220-es-74-prefer-to-declare-a-loop-variable-in-the-initializer-part-of-a-for-statement.md) | ES.74: Prefer to declare a loop variable in the initializer part of a for-statement | accepted | 2026-08-10 |
| [0221](0221-es-75-avoid-do-statements.md) | ES.75: Avoid do-statements | accepted | 2026-08-10 |
| [0222](0222-es-76-avoid-goto.md) | ES.76: Avoid goto | accepted | 2026-08-10 |
| [0223](0223-es-77-minimize-the-use-of-break-and-continue-in-loops.md) | ES.77: Minimize the use of break and continue in loops | accepted | 2026-08-10 |
| [0224](0224-es-78-dont-rely-on-implicit-fallthrough-in-switch-statements.md) | ES.78: Don't rely on implicit fallthrough in switch statements | accepted | 2026-08-10 |
| [0225](0225-es-79-use-default-to-handle-common-cases-only.md) | ES.79: Use default to handle common cases (only) | accepted | 2026-08-10 |
| [0226](0226-es-84-dont-try-to-declare-a-local-variable-with-no-name.md) | ES.84: Don't try to declare a local variable with no name | accepted | 2026-08-10 |
| [0227](0227-es-85-make-empty-statements-visible.md) | ES.85: Make empty statements visible | accepted | 2026-08-10 |
| [0228](0228-es-86-avoid-modifying-loop-control-variables-inside-the-body-of-raw-for-loops.md) | ES.86: Avoid modifying loop control variables inside the body of raw for-loops | accepted | 2026-08-10 |
| [0229](0229-es-87-dont-add-redundant-or-to-conditions.md) | ES.87: Don't add redundant == or != to conditions | accepted | 2026-08-10 |
| [0230](0230-es-100-dont-mix-signed-and-unsigned-arithmetic.md) | ES.100: Don't mix signed and unsigned arithmetic | accepted | 2026-08-10 |
| [0231](0231-es-101-use-unsigned-types-for-bit-manipulation.md) | ES.101: Use unsigned types for bit manipulation | accepted | 2026-08-10 |
| [0232](0232-es-102-use-signed-types-for-arithmetic.md) | ES.102: Use signed types for arithmetic | accepted | 2026-08-10 |
| [0233](0233-es-103-dont-overflow.md) | ES.103: Don't overflow | accepted | 2026-08-10 |
| [0234](0234-es-104-dont-underflow.md) | ES.104: Don't underflow | accepted | 2026-08-10 |
| [0235](0235-es-105-dont-divide-by-integer-zero.md) | ES.105: Don't divide by integer zero | accepted | 2026-08-10 |
| [0236](0236-es-106-dont-try-to-avoid-negative-values-by-using-unsigned.md) | ES.106: Don't try to avoid negative values by using unsigned | accepted | 2026-08-10 |
| [0237](0237-es-107-dont-use-unsigned-for-subscripts-prefer-gsl-index.md) | ES.107: Don't use unsigned for subscripts, prefer gsl::index | accepted | 2026-08-10 |
| [0238](0238-per-1-dont-optimize-without-reason.md) | Per.1: Don't optimize without reason | accepted | 2026-08-10 |
| [0239](0239-per-2-dont-optimize-prematurely.md) | Per.2: Don't optimize prematurely | accepted | 2026-08-10 |
| [0240](0240-per-3-dont-optimize-something-thats-not-performance-critical.md) | Per.3: Don't optimize something that's not performance critical | accepted | 2026-08-10 |
| [0241](0241-per-4-dont-assume-that-complicated-code-is-necessarily-faster-than-simple-code.md) | Per.4: Don't assume that complicated code is necessarily faster than simple code | accepted | 2026-08-10 |
| [0242](0242-per-5-dont-assume-that-low-level-code-is-necessarily-faster-than-high-level-code.md) | Per.5: Don't assume that low-level code is necessarily faster than high-level code | accepted | 2026-08-10 |
| [0243](0243-per-6-dont-make-claims-about-performance-without-measurements.md) | Per.6: Don't make claims about performance without measurements | accepted | 2026-08-10 |
| [0244](0244-per-7-design-to-enable-optimization.md) | Per.7: Design to enable optimization | accepted | 2026-08-10 |
| [0245](0245-per-10-rely-on-the-static-type-system.md) | Per.10: Rely on the static type system | accepted | 2026-08-10 |
| [0246](0246-per-11-move-computation-from-run-time-to-compile-time.md) | Per.11: Move computation from run time to compile time | accepted | 2026-08-10 |
| [0247](0247-per-12-eliminate-redundant-aliases.md) | Per.12: Eliminate redundant aliases | accepted | 2026-08-10 |
| [0248](0248-per-13-eliminate-redundant-indirections.md) | Per.13: Eliminate redundant indirections | accepted | 2026-08-10 |
| [0249](0249-per-14-minimize-the-number-of-allocations-and-deallocations.md) | Per.14: Minimize the number of allocations and deallocations | accepted | 2026-08-10 |
| [0250](0250-per-15-do-not-allocate-on-a-critical-branch.md) | Per.15: Do not allocate on a critical branch | accepted | 2026-08-10 |
| [0251](0251-per-16-use-compact-data-structures.md) | Per.16: Use compact data structures | accepted | 2026-08-10 |
| [0252](0252-per-17-declare-the-most-used-member-of-a-time-critical-struct-first.md) | Per.17: Declare the most used member of a time-critical struct first | accepted | 2026-08-10 |
| [0253](0253-per-18-space-is-time.md) | Per.18: Space is time | accepted | 2026-08-10 |
| [0254](0254-per-19-access-memory-predictably.md) | Per.19: Access memory predictably | accepted | 2026-08-10 |
| [0255](0255-per-30-avoid-context-switches-on-the-critical-path.md) | Per.30: Avoid context switches on the critical path | accepted | 2026-08-10 |
| [0256](0256-cp-1-assume-that-your-code-will-run-as-part-of-a-multi-threaded-program.md) | CP.1: Assume that your code will run as part of a multi-threaded program | accepted | 2026-08-10 |
| [0257](0257-cp-2-avoid-data-races.md) | CP.2: Avoid data races | accepted | 2026-08-10 |
| [0258](0258-cp-3-minimize-explicit-sharing-of-writable-data.md) | CP.3: Minimize explicit sharing of writable data | accepted | 2026-08-10 |
| [0259](0259-cp-4-think-in-terms-of-tasks-rather-than-threads.md) | CP.4: Think in terms of tasks, rather than threads | accepted | 2026-08-10 |
| [0260](0260-cp-8-dont-try-to-use-volatile-for-synchronization.md) | CP.8: Don't try to use volatile for synchronization | accepted | 2026-08-10 |
| [0261](0261-cp-9-whenever-feasible-use-tools-to-validate-your-concurrent-code.md) | CP.9: Whenever feasible use tools to validate your concurrent code | accepted | 2026-08-10 |
| [0262](0262-cp-20-use-raii-never-plain-lock-unlock.md) | CP.20: Use RAII, never plain lock()/unlock() | accepted | 2026-08-10 |
| [0263](0263-cp-21-use-std-lock-or-std-scoped-lock-to-acquire-multiple-mutexes.md) | CP.21: Use std::lock() or std::scoped_lock to acquire multiple mutexes | accepted | 2026-08-10 |
| [0264](0264-cp-22-never-call-unknown-code-while-holding-a-lock-e-g-a-callback.md) | CP.22: Never call unknown code while holding a lock (e.g., a callback) | accepted | 2026-08-10 |
| [0265](0265-cp-23-think-of-a-joining-thread-as-a-scoped-container.md) | CP.23: Think of a joining thread as a scoped container | accepted | 2026-08-10 |
| [0266](0266-cp-24-think-of-a-thread-as-a-global-container.md) | CP.24: Think of a thread as a global container | accepted | 2026-08-10 |
| [0267](0267-cp-25-prefer-gsl-joining-thread-over-std-thread.md) | CP.25: Prefer gsl::joining_thread over std::thread | accepted | 2026-08-10 |
| [0268](0268-cp-26-dont-detach-a-thread.md) | CP.26: Don't detach() a thread | accepted | 2026-08-10 |
| [0269](0269-cp-31-pass-small-amounts-of-data-between-threads-by-value-rather-than-by-reference-or-pointer.md) | CP.31: Pass small amounts of data between threads by value, rather than by reference or pointer | accepted | 2026-08-10 |
| [0270](0270-cp-32-to-share-ownership-between-unrelated-threads-use-shared-ptr.md) | CP.32: To share ownership between unrelated threads use shared_ptr | accepted | 2026-08-10 |
| [0271](0271-cp-40-minimize-context-switching.md) | CP.40: Minimize context switching | accepted | 2026-08-10 |
| [0272](0272-cp-41-minimize-thread-creation-and-destruction.md) | CP.41: Minimize thread creation and destruction | accepted | 2026-08-10 |
| [0273](0273-cp-42-dont-wait-without-a-condition.md) | CP.42: Don't wait without a condition | accepted | 2026-08-10 |
| [0274](0274-cp-43-minimize-time-spent-in-a-critical-section.md) | CP.43: Minimize time spent in a critical section | accepted | 2026-08-10 |
| [0275](0275-cp-44-remember-to-name-your-lock-guards-and-unique-locks.md) | CP.44: Remember to name your lock_guards and unique_locks | accepted | 2026-08-10 |
| [0276](0276-cp-50-define-a-mutex-together-with-the-data-it-guards-use-synchronized-value-t-where-possible.md) | CP.50: Define a mutex together with the data it guards. Use synchronized_value<T> where possible | accepted | 2026-08-10 |
| [0277](0277-cp-51-do-not-use-capturing-lambdas-that-are-coroutines.md) | CP.51: Do not use capturing lambdas that are coroutines | accepted | 2026-08-10 |
| [0278](0278-cp-52-do-not-hold-locks-or-other-synchronization-primitives-across-suspension-points.md) | CP.52: Do not hold locks or other synchronization primitives across suspension points | accepted | 2026-08-10 |
| [0279](0279-cp-53-parameters-to-coroutines-should-not-be-passed-by-reference.md) | CP.53: Parameters to coroutines should not be passed by reference | accepted | 2026-08-10 |
| [0280](0280-cp-60-use-a-future-to-return-a-value-from-a-concurrent-task.md) | CP.60: Use a future to return a value from a concurrent task | accepted | 2026-08-10 |
| [0281](0281-cp-61-use-async-to-spawn-concurrent-tasks.md) | CP.61: Use async() to spawn concurrent tasks | accepted | 2026-08-10 |
| [0282](0282-cp-100-dont-use-lock-free-programming-unless-you-absolutely-have-to.md) | CP.100: Don't use lock-free programming unless you absolutely have to | accepted | 2026-08-10 |
| [0283](0283-cp-101-distrust-your-hardware-compiler-combination.md) | CP.101: Distrust your hardware/compiler combination | accepted | 2026-08-10 |
| [0284](0284-cp-102-carefully-study-the-literature.md) | CP.102: Carefully study the literature | accepted | 2026-08-10 |
| [0285](0285-cp-110-do-not-write-your-own-double-checked-locking-for-initialization.md) | CP.110: Do not write your own double-checked locking for initialization | accepted | 2026-08-10 |
| [0286](0286-cp-111-use-a-conventional-pattern-if-you-really-need-double-checked-locking.md) | CP.111: Use a conventional pattern if you really need double-checked locking | accepted | 2026-08-10 |
| [0287](0287-cp-200-use-volatile-only-to-talk-to-non-c-memory.md) | CP.200: Use volatile only to talk to non-C++ memory | accepted | 2026-08-10 |
| [0288](0288-cp-201-signals.md) | CP.201: Signals | accepted | 2026-08-10 |
| [0289](0289-e-1-develop-an-error-handling-strategy-early-in-a-design.md) | E.1: Develop an error-handling strategy early in a design | accepted | 2026-08-10 |
| [0290](0290-e-2-throw-an-exception-to-signal-that-a-function-cant-perform-its-assigned-task.md) | E.2: Throw an exception to signal that a function can't perform its assigned task | accepted | 2026-08-10 |
| [0291](0291-e-3-use-exceptions-for-error-handling-only.md) | E.3: Use exceptions for error handling only | accepted | 2026-08-10 |
| [0292](0292-e-4-design-your-error-handling-strategy-around-invariants.md) | E.4: Design your error-handling strategy around invariants | accepted | 2026-08-10 |
| [0293](0293-e-5-let-a-constructor-establish-an-invariant-and-throw-if-it-cannot.md) | E.5: Let a constructor establish an invariant, and throw if it cannot | accepted | 2026-08-10 |
| [0294](0294-e-6-use-raii-to-prevent-leaks.md) | E.6: Use RAII to prevent leaks | accepted | 2026-08-10 |
| [0295](0295-e-7-state-your-preconditions.md) | E.7: State your preconditions | accepted | 2026-08-10 |
| [0296](0296-e-8-state-your-postconditions.md) | E.8: State your postconditions | accepted | 2026-08-10 |
| [0297](0297-e-12-use-noexcept-when-exiting-a-function-because-of-a-throw-is-impossible-or-unacceptable.md) | E.12: Use noexcept when exiting a function because of a throw is impossible or unacceptable | accepted | 2026-08-10 |
| [0298](0298-e-13-never-throw-while-being-the-direct-owner-of-an-object.md) | E.13: Never throw while being the direct owner of an object | accepted | 2026-08-10 |
| [0299](0299-e-14-use-purpose-designed-user-defined-types-as-exceptions-not-built-in-types.md) | E.14: Use purpose-designed user-defined types as exceptions (not built-in types) | accepted | 2026-08-10 |
| [0300](0300-e-15-throw-by-value-catch-exceptions-from-a-hierarchy-by-reference.md) | E.15: Throw by value, catch exceptions from a hierarchy by reference | accepted | 2026-08-10 |
| [0301](0301-e-16-destructors-deallocation-swap-and-exception-type-copy-move-construction-must-never-fail.md) | E.16: Destructors, deallocation, swap, and exception type copy/move construction must never fail | accepted | 2026-08-10 |
| [0302](0302-e-17-dont-try-to-catch-every-exception-in-every-function.md) | E.17: Don't try to catch every exception in every function | accepted | 2026-08-10 |
| [0303](0303-e-18-minimize-the-use-of-explicit-try-catch.md) | E.18: Minimize the use of explicit try/catch | accepted | 2026-08-10 |
| [0304](0304-e-19-use-a-final-action-object-to-express-cleanup-if-no-suitable-resource-handle-is-available.md) | E.19: Use a final_action object to express cleanup if no suitable resource handle is available | accepted | 2026-08-10 |
| [0305](0305-e-25-if-you-cant-throw-exceptions-simulate-raii-for-resource-management.md) | E.25: If you can't throw exceptions, simulate RAII for resource management | accepted | 2026-08-10 |
| [0306](0306-e-26-if-you-cant-throw-exceptions-consider-failing-fast.md) | E.26: If you can't throw exceptions, consider failing fast | accepted | 2026-08-10 |
| [0307](0307-e-27-if-you-cant-throw-exceptions-use-error-codes-systematically.md) | E.27: If you can't throw exceptions, use error codes systematically | accepted | 2026-08-10 |
| [0308](0308-e-28-avoid-error-handling-based-on-global-state-e-g-errno.md) | E.28: Avoid error handling based on global state (e.g. errno) | accepted | 2026-08-10 |
| [0309](0309-e-30-dont-use-exception-specifications.md) | E.30: Don't use exception specifications | accepted | 2026-08-10 |
| [0310](0310-e-31-properly-order-your-catch-clauses.md) | E.31: Properly order your catch-clauses | accepted | 2026-08-10 |
| [0311](0311-con-1-by-default-make-objects-immutable.md) | Con.1: By default, make objects immutable | accepted | 2026-08-10 |
| [0312](0312-con-2-by-default-make-member-functions-const.md) | Con.2: By default, make member functions const | accepted | 2026-08-10 |
| [0313](0313-con-3-by-default-pass-pointers-and-references-to-consts.md) | Con.3: By default, pass pointers and references to consts | accepted | 2026-08-10 |
| [0314](0314-con-4-use-const-to-define-objects-with-values-that-do-not-change-after-construction.md) | Con.4: Use const to define objects with values that do not change after construction | accepted | 2026-08-10 |
| [0315](0315-con-5-use-constexpr-for-values-that-can-be-computed-at-compile-time.md) | Con.5: Use constexpr for values that can be computed at compile time | accepted | 2026-08-10 |
| [0316](0316-t-1-use-templates-to-raise-the-level-of-abstraction-of-code.md) | T.1: Use templates to raise the level of abstraction of code | accepted | 2026-08-10 |
| [0317](0317-t-2-use-templates-to-express-algorithms-that-apply-to-many-argument-types.md) | T.2: Use templates to express algorithms that apply to many argument types | accepted | 2026-08-10 |
| [0318](0318-t-3-use-templates-to-express-containers-and-ranges.md) | T.3: Use templates to express containers and ranges | accepted | 2026-08-10 |
| [0319](0319-t-4-use-templates-to-express-syntax-tree-manipulation.md) | T.4: Use templates to express syntax tree manipulation | accepted | 2026-08-10 |
| [0320](0320-t-5-combine-generic-and-oo-techniques-to-amplify-their-strengths-not-their-costs.md) | T.5: Combine generic and OO techniques to amplify their strengths, not their costs | accepted | 2026-08-10 |
| [0321](0321-t-10-specify-concepts-for-all-template-arguments.md) | T.10: Specify concepts for all template arguments | accepted | 2026-08-10 |
| [0322](0322-t-11-whenever-possible-use-standard-concepts.md) | T.11: Whenever possible use standard concepts | accepted | 2026-08-10 |
| [0323](0323-t-12-prefer-concept-names-over-auto-for-local-variables.md) | T.12: Prefer concept names over auto for local variables | accepted | 2026-08-10 |
| [0324](0324-t-13-prefer-the-shorthand-notation-for-simple-single-type-argument-concepts.md) | T.13: Prefer the shorthand notation for simple, single-type argument concepts | accepted | 2026-08-10 |
| [0325](0325-t-20-avoid-concepts-without-meaningful-semantics.md) | T.20: Avoid "concepts" without meaningful semantics | accepted | 2026-08-10 |
| [0326](0326-t-21-require-a-complete-set-of-operations-for-a-concept.md) | T.21: Require a complete set of operations for a concept | accepted | 2026-08-10 |
| [0327](0327-t-22-specify-axioms-for-concepts.md) | T.22: Specify axioms for concepts | accepted | 2026-08-10 |
| [0328](0328-t-23-differentiate-a-refined-concept-from-its-more-general-case-by-adding-new-use-patterns.md) | T.23: Differentiate a refined concept from its more general case by adding new use patterns. | accepted | 2026-08-10 |
| [0329](0329-t-24-use-tag-classes-or-traits-to-differentiate-concepts-that-differ-only-in-semantics.md) | T.24: Use tag classes or traits to differentiate concepts that differ only in semantics. | accepted | 2026-08-10 |
| [0330](0330-t-25-avoid-complementary-constraints.md) | T.25: Avoid complementary constraints | accepted | 2026-08-10 |
| [0331](0331-t-26-prefer-to-define-concepts-in-terms-of-use-patterns-rather-than-simple-syntax.md) | T.26: Prefer to define concepts in terms of use-patterns rather than simple syntax | accepted | 2026-08-10 |
| [0332](0332-t-40-use-function-objects-to-pass-operations-to-algorithms.md) | T.40: Use function objects to pass operations to algorithms | accepted | 2026-08-10 |
| [0333](0333-t-41-require-only-essential-properties-in-a-templates-concepts.md) | T.41: Require only essential properties in a template's concepts | accepted | 2026-08-10 |
| [0334](0334-t-42-use-template-aliases-to-simplify-notation-and-hide-implementation-details.md) | T.42: Use template aliases to simplify notation and hide implementation details | accepted | 2026-08-10 |
| [0335](0335-t-43-prefer-using-over-typedef-for-defining-aliases.md) | T.43: Prefer using over typedef for defining aliases | accepted | 2026-08-10 |
| [0336](0336-t-44-use-function-templates-to-deduce-class-template-argument-types-where-feasible.md) | T.44: Use function templates to deduce class template argument types (where feasible) | accepted | 2026-08-10 |
| [0337](0337-t-46-require-template-arguments-to-be-at-least-semiregular.md) | T.46: Require template arguments to be at least semiregular | accepted | 2026-08-10 |
| [0338](0338-t-47-avoid-highly-visible-unconstrained-templates-with-common-names.md) | T.47: Avoid highly visible unconstrained templates with common names | accepted | 2026-08-10 |
| [0339](0339-t-48-if-your-compiler-does-not-support-concepts-fake-them-with-enable-if.md) | T.48: If your compiler does not support concepts, fake them with enable_if | accepted | 2026-08-10 |
| [0340](0340-t-49-where-possible-avoid-type-erasure.md) | T.49: Where possible, avoid type-erasure | accepted | 2026-08-10 |
| [0341](0341-t-60-minimize-a-templates-context-dependencies.md) | T.60: Minimize a template's context dependencies | accepted | 2026-08-10 |
| [0342](0342-t-61-do-not-over-parameterize-members-scary.md) | T.61: Do not over-parameterize members (SCARY) | accepted | 2026-08-10 |
| [0343](0343-t-62-place-non-dependent-class-template-members-in-a-non-templated-base-class.md) | T.62: Place non-dependent class template members in a non-templated base class | accepted | 2026-08-10 |
| [0344](0344-t-64-use-specialization-to-provide-alternative-implementations-of-class-templates.md) | T.64: Use specialization to provide alternative implementations of class templates | accepted | 2026-08-10 |
| [0345](0345-t-65-use-tag-dispatch-to-provide-alternative-implementations-of-a-function.md) | T.65: Use tag dispatch to provide alternative implementations of a function | accepted | 2026-08-10 |
| [0346](0346-t-67-use-specialization-to-provide-alternative-implementations-for-irregular-types.md) | T.67: Use specialization to provide alternative implementations for irregular types | accepted | 2026-08-10 |
| [0347](0347-t-68-use-rather-than-within-templates-to-avoid-ambiguities.md) | T.68: Use {} rather than () within templates to avoid ambiguities | accepted | 2026-08-10 |
| [0348](0348-t-69-inside-a-template-dont-make-an-unqualified-non-member-function-call-unless-you-intend-it-to-be-a-customization-point.md) | T.69: Inside a template, don't make an unqualified non-member function call unless you intend it to be a customization point | accepted | 2026-08-10 |
| [0349](0349-t-80-do-not-naively-templatize-a-class-hierarchy.md) | T.80: Do not naively templatize a class hierarchy | accepted | 2026-08-10 |
| [0350](0350-t-81-do-not-mix-hierarchies-and-arrays.md) | T.81: Do not mix hierarchies and arrays | accepted | 2026-08-10 |
| [0351](0351-t-82-linearize-a-hierarchy-when-virtual-functions-are-undesirable.md) | T.82: Linearize a hierarchy when virtual functions are undesirable | accepted | 2026-08-10 |
| [0352](0352-t-83-do-not-declare-a-member-function-template-virtual.md) | T.83: Do not declare a member function template virtual | accepted | 2026-08-10 |
| [0353](0353-t-84-use-a-non-template-core-implementation-to-provide-an-abi-stable-interface.md) | T.84: Use a non-template core implementation to provide an ABI-stable interface | accepted | 2026-08-10 |
| [0354](0354-t-100-use-variadic-templates-when-you-need-a-function-that-takes-a-variable-number-of-arguments-of-a-variety-of-types.md) | T.100: Use variadic templates when you need a function that takes a variable number of arguments of a variety of types | accepted | 2026-08-10 |
| [0355](0355-t-101-how-to-pass-arguments-to-a-variadic-template.md) | T.101: How to pass arguments to a variadic template | accepted | 2026-08-10 |
| [0356](0356-t-102-how-to-process-arguments-to-a-variadic-template.md) | T.102: How to process arguments to a variadic template | accepted | 2026-08-10 |
| [0357](0357-t-103-dont-use-variadic-templates-for-homogeneous-argument-lists.md) | T.103: Don't use variadic templates for homogeneous argument lists | accepted | 2026-08-10 |
| [0358](0358-t-120-use-template-metaprogramming-only-when-you-really-need-to.md) | T.120: Use template metaprogramming only when you really need to | accepted | 2026-08-10 |
| [0359](0359-t-121-use-template-metaprogramming-primarily-to-emulate-concepts.md) | T.121: Use template metaprogramming primarily to emulate concepts | accepted | 2026-08-10 |
| [0360](0360-t-122-use-templates-usually-template-aliases-to-compute-types-at-compile-time.md) | T.122: Use templates (usually template aliases) to compute types at compile time | accepted | 2026-08-10 |
| [0361](0361-t-123-use-constexpr-functions-to-compute-values-at-compile-time.md) | T.123: Use constexpr functions to compute values at compile time | accepted | 2026-08-10 |
| [0362](0362-t-124-prefer-to-use-standard-library-tmp-facilities.md) | T.124: Prefer to use standard-library TMP facilities | accepted | 2026-08-10 |
| [0363](0363-t-125-if-you-need-to-go-beyond-the-standard-library-tmp-facilities-use-an-existing-library.md) | T.125: If you need to go beyond the standard-library TMP facilities, use an existing library | accepted | 2026-08-10 |
| [0364](0364-t-140-name-all-operations-with-potential-for-reuse.md) | T.140: Name all operations with potential for reuse | accepted | 2026-08-10 |
| [0365](0365-t-141-use-an-unnamed-lambda-if-you-need-a-simple-function-object-in-one-place-only.md) | T.141: Use an unnamed lambda if you need a simple function object in one place only | superseded by 0561 | 2026-08-10 |
| [0366](0366-t-143-dont-write-unintentionally-non-generic-code.md) | T.143: Don't write unintentionally non-generic code | accepted | 2026-08-10 |
| [0367](0367-t-144-dont-specialize-function-templates.md) | T.144: Don't specialize function templates | accepted | 2026-08-10 |
| [0368](0368-t-150-check-that-a-class-matches-a-concept-using-static-assert.md) | T.150: Check that a class matches a concept using static_assert | accepted | 2026-08-10 |
| [0369](0369-cpl-1-prefer-c-to-c.md) | CPL.1: Prefer C++ to C | accepted | 2026-08-10 |
| [0370](0370-cpl-2-if-you-must-use-c-use-the-common-subset-of-c-and-c-and-compile-the-c-code-as-c.md) | CPL.2: If you must use C, use the common subset of C and C++, and compile the C code as C++ | accepted | 2026-08-10 |
| [0371](0371-cpl-3-if-you-must-use-c-for-interfaces-use-c-in-the-calling-code-using-such-interfaces.md) | CPL.3: If you must use C for interfaces, use C++ in the calling code using such interfaces | accepted | 2026-08-10 |
| [0372](0372-sf-1-use-a-cpp-suffix-for-code-files-and-h-for-interface-files-if-your-project-doesnt-already-follow-another-convention.md) | SF.1: Use a .cpp suffix for code files and .h for interface files if your project doesn't already follow another convention | accepted | 2026-08-10 |
| [0373](0373-sf-2-a-h-file-must-not-contain-object-definitions-or-non-inline-function-definitions.md) | SF.2: A .h file must not contain object definitions or non-inline function definitions | accepted | 2026-08-10 |
| [0374](0374-sf-3-use-h-files-for-all-declarations-used-in-multiple-source-files.md) | SF.3: Use .h files for all declarations used in multiple source files | accepted | 2026-08-10 |
| [0375](0375-sf-4-include-h-files-before-other-declarations-in-a-file.md) | SF.4: Include .h files before other declarations in a file | accepted | 2026-08-10 |
| [0376](0376-sf-5-a-cpp-file-must-include-the-h-file-s-that-defines-its-interface.md) | SF.5: A .cpp file must include the .h file(s) that defines its interface | accepted | 2026-08-10 |
| [0377](0377-sf-6-use-using-namespace-directives-for-transition-for-foundation-libraries-such-as-std-or-within-a-local-scope-only.md) | SF.6: Use using namespace directives for transition, for foundation libraries (such as std), or within a local scope (only) | accepted | 2026-08-10 |
| [0378](0378-sf-7-dont-write-using-namespace-at-global-scope-in-a-header-file.md) | SF.7: Don't write using namespace at global scope in a header file | accepted | 2026-08-10 |
| [0379](0379-sf-8-use-include-guards-for-all-h-files.md) | SF.8: Use #include guards for all .h files | accepted | 2026-08-10 |
| [0380](0380-sf-9-avoid-cyclic-dependencies-among-source-files.md) | SF.9: Avoid cyclic dependencies among source files | accepted | 2026-08-10 |
| [0381](0381-sf-10-avoid-dependencies-on-implicitly-included-names.md) | SF.10: Avoid dependencies on implicitly #included names | accepted | 2026-08-10 |
| [0382](0382-sf-11-header-files-should-be-self-contained.md) | SF.11: Header files should be self-contained | accepted | 2026-08-10 |
| [0383](0383-sf-12-prefer-the-quoted-form-of-include-for-files-relative-to-the-including-file-and-the-angle-bracket-form-everywhere-else.md) | SF.12: Prefer the quoted form of #include for files relative to the including file and the angle bracket form everywhere else | accepted | 2026-08-10 |
| [0384](0384-sf-20-use-namespaces-to-express-logical-structure.md) | SF.20: Use namespaces to express logical structure | accepted | 2026-08-10 |
| [0385](0385-sf-21-dont-use-an-unnamed-anonymous-namespace-in-a-header.md) | SF.21: Don't use an unnamed (anonymous) namespace in a header | accepted | 2026-08-10 |
| [0386](0386-sf-22-use-an-unnamed-anonymous-namespace-for-all-internal-non-exported-entities.md) | SF.22: Use an unnamed (anonymous) namespace for all internal/non-exported entities | accepted | 2026-08-10 |
| [0387](0387-sl-1-use-libraries-wherever-possible.md) | SL.1: Use libraries wherever possible | accepted | 2026-08-10 |
| [0388](0388-sl-2-prefer-the-standard-library-to-other-libraries.md) | SL.2: Prefer the standard library to other libraries | accepted | 2026-08-10 |
| [0389](0389-sl-3-do-not-add-non-standard-entities-to-namespace-std.md) | SL.3: Do not add non-standard entities to namespace std | accepted | 2026-08-10 |
| [0390](0390-sl-4-use-the-standard-library-in-a-type-safe-manner.md) | SL.4: Use the standard library in a type-safe manner | accepted | 2026-08-10 |
| [0391](0391-sl-con-1-prefer-using-stl-array-or-vector-instead-of-a-c-array.md) | SL.con.1: Prefer using STL array or vector instead of a C array | accepted | 2026-08-10 |
| [0392](0392-sl-con-2-prefer-using-stl-vector-by-default-unless-you-have-a-reason-to-use-a-different-container.md) | SL.con.2: Prefer using STL vector by default unless you have a reason to use a different container | accepted | 2026-08-10 |
| [0393](0393-sl-con-3-avoid-bounds-errors.md) | SL.con.3: Avoid bounds errors | accepted | 2026-08-10 |
| [0394](0394-sl-con-4-dont-use-memset-or-memcpy-for-arguments-that-are-not-trivially-copyable.md) | SL.con.4: Don't use memset or memcpy for arguments that are not trivially-copyable | accepted | 2026-08-10 |
| [0395](0395-sl-str-1-use-std-string-to-own-character-sequences.md) | SL.str.1: Use std::string to own character sequences | accepted | 2026-08-10 |
| [0396](0396-sl-str-2-use-std-string-view-or-gsl-span-char-to-refer-to-character-sequences.md) | SL.str.2: Use std::string_view or gsl::span<char> to refer to character sequences | accepted | 2026-08-10 |
| [0397](0397-sl-str-3-use-zstring-or-czstring-to-refer-to-a-c-style-zero-terminated-sequence-of-characters.md) | SL.str.3: Use zstring or czstring to refer to a C-style, zero-terminated, sequence of characters | accepted | 2026-08-10 |
| [0398](0398-sl-str-4-use-char-to-refer-to-a-single-character.md) | SL.str.4: Use char* to refer to a single character | accepted | 2026-08-10 |
| [0399](0399-sl-str-5-use-std-byte-to-refer-to-byte-values-that-do-not-necessarily-represent-characters.md) | SL.str.5: Use std::byte to refer to byte values that do not necessarily represent characters | accepted | 2026-08-10 |
| [0400](0400-sl-str-10-use-std-string-when-you-need-to-perform-locale-sensitive-string-operations.md) | SL.str.10: Use std::string when you need to perform locale-sensitive string operations | accepted | 2026-08-10 |
| [0401](0401-sl-str-11-use-gsl-span-char-rather-than-std-string-view-when-you-need-to-mutate-a-string.md) | SL.str.11: Use gsl::span<char> rather than std::string_view when you need to mutate a string | accepted | 2026-08-10 |
| [0402](0402-sl-str-12-use-the-s-suffix-for-string-literals-meant-to-be-standard-library-strings.md) | SL.str.12: Use the s suffix for string literals meant to be standard-library strings | accepted | 2026-08-10 |
| [0403](0403-sl-io-1-use-character-level-input-only-when-you-have-to.md) | SL.io.1: Use character-level input only when you have to | accepted | 2026-08-10 |
| [0404](0404-sl-io-2-when-reading-always-consider-ill-formed-input.md) | SL.io.2: When reading, always consider ill-formed input | accepted | 2026-08-10 |
| [0405](0405-sl-io-3-prefer-iostreams-for-i-o.md) | SL.io.3: Prefer iostreams for I/O | accepted | 2026-08-10 |
| [0406](0406-sl-io-10-unless-you-use-printf-family-functions-call-ios-base-sync-with-stdio-false.md) | SL.io.10: Unless you use printf-family functions call ios_base::sync_with_stdio(false) | accepted | 2026-08-10 |
| [0407](0407-sl-io-50-avoid-endl.md) | SL.io.50: Avoid endl | accepted | 2026-08-10 |
| [0408](0408-sl-c-1-dont-use-setjmp-longjmp.md) | SL.C.1: Don't use setjmp/longjmp | accepted | 2026-08-10 |
| [0409](0409-a-1-separate-stable-code-from-less-stable-code.md) | A.1: Separate stable code from less stable code | accepted | 2026-08-10 |
| [0410](0410-a-2-express-potentially-reusable-parts-as-a-library.md) | A.2: Express potentially reusable parts as a library | accepted | 2026-08-10 |
| [0411](0411-a-4-there-should-be-no-cycles-among-libraries.md) | A.4: There should be no cycles among libraries | accepted | 2026-08-10 |
| [0412](0412-nr-1-dont-insist-that-all-declarations-should-be-at-the-top-of-a-function.md) | NR.1: Don't insist that all declarations should be at the top of a function | accepted | 2026-08-10 |
| [0413](0413-nr-2-dont-insist-to-have-only-a-single-return-statement-in-a-function.md) | NR.2: Don't insist to have only a single return-statement in a function | accepted | 2026-08-10 |
| [0414](0414-nr-3-dont-avoid-exceptions.md) | NR.3: Don't avoid exceptions | accepted | 2026-08-10 |
| [0415](0415-nr-4-dont-insist-on-placing-each-class-declaration-in-its-own-source-file.md) | NR.4: Don't insist on placing each class declaration in its own source file | accepted | 2026-08-10 |
| [0416](0416-nr-5-dont-use-two-phase-initialization.md) | NR.5: Don't use two-phase initialization | accepted | 2026-08-10 |
| [0417](0417-nr-6-dont-place-all-cleanup-actions-at-the-end-of-a-function-and-goto-exit.md) | NR.6: Don't place all cleanup actions at the end of a function and goto exit | accepted | 2026-08-10 |
| [0418](0418-nr-7-dont-make-all-data-members-protected.md) | NR.7: Don't make all data members protected | accepted | 2026-08-10 |
| [0419](0419-nl-1-dont-say-in-comments-what-can-be-clearly-stated-in-code.md) | NL.1: Don't say in comments what can be clearly stated in code | accepted | 2026-08-10 |
| [0420](0420-nl-2-state-intent-in-comments.md) | NL.2: State intent in comments | superseded by 0562 | 2026-08-10 |
| [0421](0421-nl-3-keep-comments-crisp.md) | NL.3: Keep comments crisp | accepted | 2026-08-10 |
| [0422](0422-nl-4-maintain-a-consistent-indentation-style.md) | NL.4: Maintain a consistent indentation style | accepted | 2026-08-10 |
| [0423](0423-nl-5-avoid-encoding-type-information-in-names.md) | NL.5: Avoid encoding type information in names | accepted | 2026-08-10 |
| [0424](0424-nl-7-make-the-length-of-a-name-roughly-proportional-to-the-length-of-its-scope.md) | NL.7: Make the length of a name roughly proportional to the length of its scope | superseded by 0559 | 2026-08-10 |
| [0425](0425-nl-8-use-a-consistent-naming-style.md) | NL.8: Use a consistent naming style | accepted | 2026-08-10 |
| [0426](0426-nl-9-use-all-caps-for-macro-names-only.md) | NL.9: Use ALL_CAPS for macro names only | accepted | 2026-08-10 |
| [0427](0427-nl-10-prefer-underscore-style-names.md) | NL.10: Prefer underscore_style names | accepted | 2026-08-10 |
| [0428](0428-nl-11-make-literals-readable.md) | NL.11: Make literals readable | accepted | 2026-08-10 |
| [0429](0429-nl-15-use-spaces-sparingly.md) | NL.15: Use spaces sparingly | accepted | 2026-08-10 |
| [0430](0430-nl-16-use-a-conventional-class-member-declaration-order.md) | NL.16: Use a conventional class member declaration order | accepted | 2026-08-10 |
| [0431](0431-nl-17-use-k-r-derived-layout.md) | NL.17: Use K&R-derived layout | accepted | 2026-08-10 |
| [0432](0432-nl-18-use-c-style-declarator-layout.md) | NL.18: Use C++-style declarator layout | accepted | 2026-08-10 |
| [0433](0433-nl-19-avoid-names-that-are-easily-misread.md) | NL.19: Avoid names that are easily misread | accepted | 2026-08-10 |
| [0434](0434-nl-20-dont-place-two-statements-on-the-same-line.md) | NL.20: Don't place two statements on the same line | accepted | 2026-08-10 |
| [0435](0435-nl-21-declare-one-name-only-per-declaration.md) | NL.21: Declare one name (only) per declaration | accepted | 2026-08-10 |
| [0436](0436-nl-25-dont-use-void-as-an-argument-type.md) | NL.25: Don't use void as an argument type | accepted | 2026-08-10 |
| [0437](0437-nl-26-use-conventional-const-notation.md) | NL.26: Use conventional const notation | accepted | 2026-08-10 |
| [0438](0438-f-1-package-meaningful-operations-as-carefully-named-functions.md) | F.1: "Package" meaningful operations as carefully named functions | accepted | 2026-08-10 |
| [0439](0439-f-2-a-function-should-perform-a-single-logical-operation.md) | F.2: A function should perform a single logical operation | accepted | 2026-08-10 |
| [0440](0440-f-3-keep-functions-short-and-simple.md) | F.3: Keep functions short and simple | accepted | 2026-08-10 |
| [0441](0441-f-4-if-a-function-might-have-to-be-evaluated-at-compile-time-declare-it-constexpr.md) | F.4: If a function might have to be evaluated at compile time, declare it constexpr | accepted | 2026-08-10 |
| [0442](0442-f-5-if-a-function-is-very-small-and-time-critical-declare-it-inline.md) | F.5: If a function is very small and time-critical, declare it inline | accepted | 2026-08-10 |
| [0443](0443-f-6-if-your-function-must-not-throw-declare-it-noexcept.md) | F.6: If your function must not throw, declare it noexcept | accepted | 2026-08-10 |
| [0444](0444-f-7-for-general-use-take-t-or-t-arguments-rather-than-smart-pointers.md) | F.7: For general use, take T* or T& arguments rather than smart pointers | accepted | 2026-08-10 |
| [0445](0445-f-8-prefer-pure-functions.md) | F.8: Prefer pure functions | accepted | 2026-08-10 |
| [0446](0446-f-9-unused-parameters-should-be-unnamed.md) | F.9: Unused parameters should be unnamed | accepted | 2026-08-10 |
| [0447](0447-f-15-prefer-simple-and-conventional-ways-of-passing-information.md) | F.15: Prefer simple and conventional ways of passing information | accepted | 2026-08-10 |
| [0448](0448-f-16-for-in-parameters-pass-cheaply-copied-types-by-value-and-others-by-reference-to-const.md) | F.16: For "in" parameters, pass cheaply-copied types by value and others by reference to const | accepted | 2026-08-10 |
| [0449](0449-f-17-for-in-out-parameters-pass-by-reference-to-non-const.md) | F.17: For "in-out" parameters, pass by reference to non-const | accepted | 2026-08-10 |
| [0450](0450-f-18-for-will-move-from-parameters-pass-by-x-and-std-move-the-parameter.md) | F.18: For "will-move-from" parameters, pass by X&& and std::move the parameter | accepted | 2026-08-10 |
| [0451](0451-f-19-for-forward-parameters-pass-by-tp-and-only-std-forward-the-parameter.md) | F.19: For "forward" parameters, pass by TP&& and only std::forward the parameter | accepted | 2026-08-10 |
| [0452](0452-f-20-for-out-output-values-prefer-return-values-to-output-parameters.md) | F.20: For "out" output values, prefer return values to output parameters | accepted | 2026-08-10 |
| [0453](0453-f-21-to-return-multiple-out-values-prefer-returning-a-struct-or-tuple.md) | F.21: To return multiple "out" values, prefer returning a struct or tuple | accepted | 2026-08-10 |
| [0454](0454-f-60-prefer-t-over-t-when-no-argument-is-a-valid-option.md) | F.60: Prefer T* over T& when "no argument" is a valid option | accepted | 2026-08-10 |
| [0455](0455-f-22-use-t-or-owner-t-to-designate-a-single-object.md) | F.22: Use T* or owner<T*> to designate a single object | accepted | 2026-08-10 |
| [0456](0456-f-23-use-a-not-null-t-to-indicate-that-null-is-not-a-valid-value.md) | F.23: Use a not_null<T> to indicate that "null" is not a valid value | accepted | 2026-08-10 |
| [0457](0457-f-24-use-a-span-t-or-a-span-p-t-to-designate-a-half-open-sequence.md) | F.24: Use a span<T> or a span_p<T> to designate a half-open sequence | accepted | 2026-08-10 |
| [0458](0458-f-25-use-a-zstring-or-a-not-null-zstring-to-designate-a-c-style-string.md) | F.25: Use a zstring or a not_null<zstring> to designate a C-style string | accepted | 2026-08-10 |
| [0459](0459-f-26-use-a-unique-ptr-t-to-transfer-ownership-where-a-pointer-is-needed.md) | F.26: Use a unique_ptr<T> to transfer ownership where a pointer is needed | accepted | 2026-08-10 |
| [0460](0460-f-27-use-a-shared-ptr-t-to-share-ownership.md) | F.27: Use a shared_ptr<T> to share ownership | accepted | 2026-08-10 |
| [0461](0461-f-42-return-a-t-to-indicate-a-position-only.md) | F.42: Return a T* to indicate a position (only) | accepted | 2026-08-10 |
| [0462](0462-f-43-never-directly-or-indirectly-return-a-pointer-or-a-reference-to-a-local-object.md) | F.43: Never (directly or indirectly) return a pointer or a reference to a local object | accepted | 2026-08-10 |
| [0463](0463-f-44-return-a-t-when-copy-is-undesirable-and-returning-no-object-isnt-needed.md) | F.44: Return a T& when copy is undesirable and "returning no object" isn't needed | accepted | 2026-08-10 |
| [0464](0464-f-45-dont-return-a-t.md) | F.45: Don't return a T&& | accepted | 2026-08-10 |
| [0465](0465-f-46-int-is-the-return-type-for-main.md) | F.46: int is the return type for main() | accepted | 2026-08-10 |
| [0466](0466-f-47-return-t-from-assignment-operators.md) | F.47: Return T& from assignment operators | accepted | 2026-08-10 |
| [0467](0467-f-48-dont-return-std-move-local.md) | F.48: Don't return std::move(local) | accepted | 2026-08-10 |
| [0468](0468-f-50-use-a-lambda-when-a-function-wont-do-to-capture-local-variables-or-to-write-a-local-function.md) | F.50: Use a lambda when a function won't do (to capture local variables, or to write a local function) | accepted | 2026-08-10 |
| [0469](0469-f-51-where-there-is-a-choice-prefer-default-arguments-over-overloading.md) | F.51: Where there is a choice, prefer default arguments over overloading | accepted | 2026-08-10 |
| [0470](0470-f-52-prefer-capturing-by-reference-in-lambdas-that-will-be-used-locally-including-passed-to-algorithms.md) | F.52: Prefer capturing by reference in lambdas that will be used locally, including passed to algorithms | accepted | 2026-08-10 |
| [0471](0471-f-53-avoid-capturing-by-reference-in-lambdas-that-will-be-used-non-locally-including-returned-stored-on-the-heap-or-passed-to-another-thread.md) | F.53: Avoid capturing by reference in lambdas that will be used non-locally, including returned, stored on the heap, or passed to another thread | accepted | 2026-08-10 |
| [0472](0472-f-54-if-you-capture-this-capture-all-variables-explicitly-no-default-capture.md) | F.54: If you capture this, capture all variables explicitly (no default capture) | accepted | 2026-08-10 |
| [0473](0473-f-55-dont-use-va-arg-arguments.md) | F.55: Don't use va_arg arguments | accepted | 2026-08-10 |
| [0474](0474-f-56-avoid-unnecessary-condition-nesting.md) | F.56: Avoid unnecessary condition nesting | accepted | 2026-08-10 |
| [0475](0475-ask-every-modal-question-through-modalquestion.md) | Ask every modal question through ModalQuestion | proposed | 2026-08-11 |
| [0476](0476-load-a-tab-whose-file-appears-rather-than-only-clearing-its-missing-mark.md) | Load a tab whose file appears, rather than only clearing its missing mark | proposed | 2026-08-11 |
| [0477](0477-draw-the-title-bar-in-the-application.md) | Draw the title bar in the application | accepted | 2026-08-11 |
| [0478](0478-route-menu-items-and-shortcuts-through-commands.md) | Route menu items and shortcuts through commands | proposed | 2026-08-12 |
| [0479](0479-settle-one-house-style-for-naming-formatting-and-layout.md) | Settle one house style for naming, formatting and layout | accepted | 2026-08-12 |
| [0480](0480-drive-input-from-events-and-leave-the-drawn-gestures-to-the-gui.md) | Drive input from events and leave the drawn gestures to the GUI | accepted | 2026-08-13 |
| [0481](0481-lex-one-line-at-a-time-carrying-a-resumable-state.md) | Lex one line at a time carrying a resumable state | proposed | 2026-08-13 |
| [0482](0482-keep-the-language-layer-in-graphite-rather-than-core.md) | Keep the language layer in Graphite rather than Core | proposed | 2026-08-13 |
| [0483](0483-show-settings-as-sections-inside-one-panel.md) | Show settings as sections inside one panel | superseded by 0573 | 2026-08-13 |
| [0484](0484-keep-cross-line-lexer-state-opaque.md) | Keep cross-line lexer state opaque | proposed | 2026-08-14 |
| [0485](0485-take-code-intelligence-from-external-language-servers.md) | Take code intelligence from external language servers | proposed | 2026-08-16 |
| [0486](0486-insert-non-document-rows-above-rowmap-rather-than-inside-it.md) | Insert non-document rows above RowMap rather than inside it | proposed | 2026-08-16 |
| [0487](0487-correct-adr-0482s-premise-about-project-data.md) | Correct ADR 0482's premise about project data | proposed | 2026-08-16 |
| [0488](0488-ship-clangd-with-the-release.md) | Ship clangd with the release | superseded by 0577 | 2026-08-16 |
| [0489](0489-open-a-previewed-file-with-the-language-server.md) | Open a previewed file with the language server | proposed | 2026-08-20 |
| [0490](0490-colour-the-application-chrome-from-the-active-scheme.md) | Colour the application chrome from the active scheme | proposed | 2026-08-20 |
| [0491](0491-keep-git-data-in-core-and-git-freshness-in-graphite.md) | Keep git data in Core and git freshness in Graphite | proposed | 2026-08-20 |
| [0492](0492-give-each-worker-thread-its-own-repository-handle.md) | Give each worker thread its own repository handle | proposed | 2026-08-20 |
| [0493](0493-draw-the-git-view-as-a-centrally-docked-panel.md) | Draw the git view as a centrally docked panel | proposed | 2026-08-20 |
| [0494](0494-correct-adr-0493-s-premise-about-document-tabs.md) | Correct ADR 0493's premise about document tabs | proposed | 2026-08-21 |
| [0495](0495-draw-a-side-by-side-diff-in-one-scrolling-region.md) | Draw a side-by-side diff in one scrolling region | proposed | 2026-08-21 |
| [0496](0496-filter-the-history-in-the-walk-not-in-the-page.md) | Filter the history in the walk, not in the page | proposed | 2026-08-21 |
| [0497](0497-name-a-file-in-a-history-filter-by-name-or-pattern-not-by-path.md) | Name a file in a history filter by name or pattern, not by path | proposed | 2026-08-21 |
| [0498](0498-tell-the-application-when-the-repository-s-own-state-changes.md) | Tell the application when the repository's own state changes | proposed | 2026-08-21 |
| [0499](0499-let-core-repository-write-not-only-read.md) | Let Core::Repository write, not only read | proposed | 2026-08-21 |
| [0500](0500-serialise-git-writes-in-one-queue.md) | Serialise git writes in one queue | proposed | 2026-08-21 |
| [0501](0501-announce-a-write-before-the-editor-sees-it.md) | Announce a write before the editor sees it | proposed | 2026-08-21 |
| [0502](0502-read-a-working-tree-file-s-text-from-the-index-and-the-disk.md) | Read a working tree file's text from the index and the disk | proposed | 2026-08-22 |
| [0503](0503-anchor-a-console-selection-to-a-log-entry-s-sequence-number.md) | Anchor a console selection to a log entry's sequence number | proposed | 2026-08-23 |
| [0504](0504-treat-an-untracked-file-as-an-added-file-in-a-working-tree-diff.md) | Treat an untracked file as an added file in a working tree diff | proposed | 2026-08-23 |
| [0505](0505-hold-diagnostics-in-a-model-both-the-editor-and-the-panel-read.md) | Hold diagnostics in a model both the editor and the panel read | proposed | 2026-08-25 |
| [0506](0506-dim-a-diagnostic-the-buffer-has-outrun-rather-than-dropping-it.md) | Dim a diagnostic the buffer has outrun rather than dropping it | proposed | 2026-08-25 |
| [0507](0507-correct-adr-0482-s-premise-about-what-stays-out-of-lang.md) | Correct ADR 0482's premise about what stays out of `Lang` | proposed | 2026-08-25 |
| [0508](0508-let-the-selected-file-win-over-the-selected-commit-in-the-git-preview-pane.md) | Let the selected file win over the selected commit in the git preview pane | proposed | 2026-08-29 |
| [0509](0509-write-the-terminal-emulator-rather-than-adopt-one.md) | Write the terminal emulator rather than adopt one | proposed | 2026-08-29 |
| [0510](0510-make-every-panel-name-the-view-menu-group-it-is-drawn-under.md) | Make every panel name the view menu group it is drawn under | proposed | 2026-09-07 |
| [0511](0511-analyse-the-workspace-ahead-of-the-reader-several-files-at-a-time.md) | Analyse the workspace ahead of the reader, several files at a time | proposed | 2026-09-14 |
| [0512](0512-name-one-owner-for-every-document-opened-with-a-language-server.md) | Name one owner for every document opened with a language server | proposed | 2026-09-14 |
| [0513](0513-keep-the-workspace-analysis-on-disk-and-discard-it-on-any-doubt.md) | Keep the workspace analysis on disk and discard it on any doubt | proposed | 2026-09-15 |
| [0514](0514-expand-an-untracked-directory-by-asking-git-about-that-directory-alone.md) | Expand an untracked directory by asking git about that directory alone | proposed | 2026-09-22 |
| [0515](0515-ask-before-every-discard-in-the-changes-panel.md) | Ask before every discard in the changes panel | proposed | 2026-09-22 |
| [0516](0516-animate-the-ui-through-a-core-facade-over-imanim.md) | Animate the UI through a Core facade over ImAnim | proposed | 2026-09-23 |
| [0517](0517-read-claude-transcripts-through-a-lazy-index-with-cached-summaries.md) | Read Claude transcripts through a lazy index with cached summaries | proposed | 2026-09-23 |
| [0518](0518-add-implot-as-a-core-dependency.md) | Add ImPlot as a Core dependency | proposed | 2026-09-23 |
| [0519](0519-run-claude-in-the-ide-s-terminal-rather-than-a-chat-of-our-own.md) | Run Claude in the IDE's terminal rather than a chat of our own | proposed | 2026-09-23 |
| [0520](0520-read-what-an-attachment-sent-from-its-rendered-field.md) | Read what an attachment sent from its rendered field | proposed | 2026-09-24 |
| [0521](0521-show-what-a-request-sent-by-size-not-by-guessed-tokens.md) | Show what a request sent by size, not by guessed tokens | proposed | 2026-09-24 |
| [0522](0522-persist-session-state-in-two-scopes-global-window-and-panels-per-workspace-tabs.md) | Persist session state in two scopes: global window and panels, per-workspace tabs | proposed | 2026-09-25 |
| [0523](0523-draw-change-markers-from-the-live-buffer-against-the-index-and-head.md) | Draw change markers from the live buffer against the index and HEAD | proposed | 2026-09-26 |
| [0524](0524-hold-change-markers-in-a-model-of-their-own-apart-from-gitmodel.md) | Hold change markers in a model of their own apart from GitModel | proposed | 2026-09-26 |
| [0525](0525-reserve-a-change-column-in-the-gutter-whether-or-not-git-is-present.md) | Reserve a change column in the gutter whether or not git is present | proposed | 2026-09-26 |
| [0526](0526-highlight-what-a-changed-line-lost-by-its-shared-prefix-and-suffix.md) | Highlight what a changed line lost by its shared prefix and suffix | proposed | 2026-09-27 |
| [0527](0527-show-change-markers-on-the-editor-s-vertical-scrollbar.md) | Show change markers on the editor's vertical scrollbar | proposed | 2026-09-27 |
| [0528](0528-close-windows-from-outside-through-their-owner-s-own-close-path.md) | Close windows from outside through their owner's own close path | accepted | 2026-09-27 |
| [0529](0529-run-system-dialogs-on-a-thread-of-their-own.md) | Run system dialogs on a thread of their own | accepted | 2026-09-29 |
| [0530](0530-draw-a-frame-only-when-there-is-reason-to.md) | Draw a frame only when there is reason to | proposed | 2026-09-29 |
| [0531](0531-keep-per-user-state-in-the-user-s-profile-not-beside-the-executable.md) | Keep per-user state in the user's profile, not beside the executable | proposed | 2026-09-30 |
| [0532](0532-open-an-unknown-folder-in-restricted-mode-until-the-user-trusts-it.md) | Open an unknown folder in restricted mode until the user trusts it | proposed | 2026-09-30 |
| [0533](0533-recover-from-imgui-usage-errors-outside-debug-and-define-ndebug-in-optimised-builds.md) | Recover from ImGui usage errors outside Debug and define NDEBUG in optimised builds | proposed | 2026-10-01 |
| [0534](0534-scale-the-interface-by-the-monitor-and-by-a-user-setting-and-keep-code-text-size-separate.md) | Scale the interface by the monitor and by a user setting, and keep code text size separate | proposed | 2026-10-01 |
| [0535](0535-show-git-state-in-the-status-bar-read-only-and-keep-git-actions-in-the-panels.md) | Show git state in the status bar read-only and keep git actions in the panels | proposed | 2026-10-01 |
| [0536](0536-initialise-a-repository-without-asking-for-confirmation.md) | Initialise a repository without asking for confirmation | proposed | 2026-10-01 |
| [0537](0537-mark-result-nodiscard-on-the-type-not-on-functions.md) | Mark Result nodiscard on the type, not on functions | accepted | 2026-10-01 |
| [0538](0538-refresh-the-working-tree-diff-when-the-repository-changes-not-when-it-is-asked-for.md) | Refresh the working-tree diff when the repository changes, not when it is asked for | proposed | 2026-10-01 |
| [0539](0539-plan-every-checkout-and-ask-before-it-would-overwrite-work.md) | Plan every checkout and ask before it would overwrite work | proposed | 2026-10-01 |
| [0540](0540-discard-or-stash-every-change-on-the-way-to-a-checkout-untracked-files-only-on-request.md) | Discard or stash every change on the way to a checkout, untracked files only on request | proposed | 2026-10-01 |
| [0541](0541-put-branch-and-stash-actions-in-the-git-panel-s-right-click-menus.md) | Put branch and stash actions in the Git panel's right-click menus | proposed | 2026-10-01 |
| [0542](0542-build-the-application-as-a-static-library-its-tests-link.md) | Build the application as a static library its tests link | proposed | 2026-10-01 |
| [0543](0543-load-the-settings-file-once-and-merge-its-changes-into-it-at-the-end-of-a-frame.md) | Load the settings file once and merge its changes into it at the end of a frame | proposed | 2026-10-01 |
| [0544](0544-draw-each-terminal-as-a-window-of-its-own-docked-beside-the-last-one-used.md) | Draw each terminal as a window of its own, docked beside the last one used | proposed | 2026-10-01 |
| [0545](0545-open-a-subagent-s-conversation-inside-its-session-s-window.md) | Open a subagent's conversation inside its session's window | proposed | 2026-10-01 |
| [0546](0546-route-editor-scoped-key-commands-from-the-focused-editor.md) | Route editor-scoped key commands from the focused editor | proposed | 2026-10-02 |
| [0547](0547-make-the-active-document-follow-keyboard-focus-into-its-editor.md) | Make the active document follow keyboard focus into its editor | proposed | 2026-10-02 |
| [0548](0548-draw-interactive-tooltips-as-non-focusing-windows-rather-than-imgui-tooltips.md) | Draw interactive tooltips as non-focusing windows rather than ImGui tooltips | proposed | 2026-10-02 |
| [0549](0549-anchor-the-terminal-buttons-to-the-whole-bottom-row-and-list-new-terminals-in-the-view-menu.md) | Anchor the terminal buttons to the whole bottom row and list new terminals in the View menu | proposed | 2026-10-03 |
| [0550](0550-lay-out-sources-by-feature-in-whole-word-folders-mirrored-by-the-tests.md) | Lay out sources by feature in whole-word folders mirrored by the tests | proposed | 2026-10-03 |
| [0551](0551-open-a-result-with-one-click-model-shared-by-every-list-that-opens-files.md) | Open a result with one click model shared by every list that opens files | proposed | 2026-10-04 |
| [0552](0552-give-a-command-a-separate-label-for-the-key-bindings-list.md) | Give a command a separate label for the key bindings list | proposed | 2026-10-04 |
| [0553](0553-keep-the-commit-message-readable-in-a-narrow-history-and-ask-before-every-detached-checkout.md) | Keep the commit message readable in a narrow history and ask before every detached checkout | proposed | 2026-10-04 |
| [0554](0554-refuse-a-commit-that-records-head-s-tree-unless-it-amends-or-concludes-a-merge.md) | Refuse a commit that records HEAD's tree unless it amends or concludes a merge | proposed | 2026-10-04 |
| [0555](0555-share-one-text-selection-model-across-views-that-show-text.md) | Share one text selection model across views that show text | proposed | 2026-10-04 |
| [0556](0556-show-git-status-in-the-file-tree-solution-view-and-editor-tabs.md) | Show git status in the file tree, solution view and editor tabs | proposed | 2026-10-04 |
| [0557](0557-report-a-failed-action-in-a-toast-rather-than-in-the-panel-that-issued-it.md) | Report a failed action in a toast rather than in the panel that issued it | proposed | 2026-10-04 |
| [0558](0558-write-code-a-junior-engineer-can-read-like-prose.md) | Write code a junior engineer can read like prose | accepted | 2026-10-05 |
| [0559](0559-name-every-variable-for-its-role-loop-indices-included.md) | Name every variable for its role, loop indices included | accepted | 2026-10-05 |
| [0560](0560-spell-out-a-variable-s-type-unless-the-same-line-already-states-it.md) | Spell out a variable's type unless the same line already states it | accepted | 2026-10-05 |
| [0561](0561-write-searches-as-named-loops-and-give-every-predicate-a-name.md) | Write searches as named loops and give every predicate a name | accepted | 2026-10-05 |
| [0562](0562-say-it-in-code-and-keep-a-comment-only-as-a-warning.md) | Say it in code and keep a comment only as a warning | accepted | 2026-10-05 |
| [0563](0563-write-concrete-code-and-keep-templates-for-vocabulary-types.md) | Write concrete code and keep templates for vocabulary types | accepted | 2026-10-05 |
| [0564](0564-show-a-toast-s-countdown-by-draining-its-border.md) | Show a toast's countdown by draining its border | proposed | 2026-10-06 |
| [0565](0565-keep-a-session-history-of-toasts-behind-a-bell-in-the-status-bar.md) | Keep a session history of toasts behind a bell in the status bar | proposed | 2026-10-07 |
| [0566](0566-store-nested-values-in-graphite-json.md) | Store nested values in graphite.json | proposed | 2026-10-09 |
| [0567](0567-keep-server-specific-behaviour-in-adapters.md) | Keep server-specific behaviour in adapters | proposed | 2026-10-09 |
| [0568](0568-colour-a-language-through-tree-sitter.md) | Colour a language through tree-sitter | proposed | 2026-10-10 |
| [0569](0569-install-a-language-server-on-request-from-its-own-release-channel.md) | Install a language server on request from its own release channel | proposed | 2026-10-10 |
| [0570](0570-run-a-language-server-from-graphite-s-install-once-its-runtime-is-found.md) | Run a language server from Graphite's install once its runtime is found | proposed | 2026-10-11 |
| [0571](0571-choose-and-install-language-servers-from-settings-and-the-editor.md) | Choose and install language servers from Settings and the editor | proposed | 2026-10-11 |
| [0572](0572-serve-c-within-the-selected-solution-and-parse-each-server-s-hover-in-its-adapter.md) | Serve C# within the selected solution and parse each server's hover in its adapter | proposed | 2026-10-11 |
| [0573](0573-describe-settings-pages-as-data-and-draw-them-with-one-layout.md) | Describe settings pages as data and draw them with one layout | accepted | 2026-10-11 |
| [0574](0574-list-what-a-settings-block-holds-as-rows-of-its-own-in-the-search.md) | List what a settings block holds as rows of its own in the search | accepted | 2026-10-11 |
| [0575](0575-filter-the-file-tree-against-a-listing-of-the-whole-folder-read-in-the-background.md) | Filter the file tree against a listing of the whole folder read in the background | proposed | 2026-10-11 |
| [0576](0576-draw-a-loading-indicator-through-a-core-facade-over-imspinner.md) | Draw a loading indicator through a Core facade over imspinner | proposed | 2026-10-11 |
| [0577](0577-install-clangd-on-request-like-every-other-language-server.md) | Install clangd on request like every other language server | proposed | 2026-10-12 |
| [0578](0578-let-a-language-server-leave-on-its-own-and-keep-its-temporary-files-where-graphite-sweeps-them.md) | Let a language server leave on its own and keep its temporary files where Graphite sweeps them | proposed | 2026-10-12 |
| [0579](0579-offer-open-beside-a-found-colour-and-scroll-its-page-to-it.md) | Offer Open beside a found colour and scroll its page to it | accepted | 2026-10-13 |
| [0580](0580-review-a-branch-against-its-merge-base-with-the-base-taken-from-head.md) | Review a branch against its merge base with the base taken from HEAD | proposed | 2026-10-13 |
| [0581](0581-show-a-branch-s-uncommitted-work-as-a-group-of-its-own-in-its-review.md) | Show a branch's uncommitted work as a group of its own in its review | proposed | 2026-10-13 |
| [0582](0582-key-a-viewed-mark-by-its-group-its-path-and-the-pair-of-blob-ids.md) | Key a viewed mark by its group, its path and the pair of blob ids | proposed | 2026-10-13 |
| [0583](0583-keep-review-marks-in-their-own-file-beside-the-window-state.md) | Keep review marks in their own file beside the window state | proposed | 2026-10-13 |
| [0584](0584-read-a-publication-without-a-version-as-describing-the-version-last-sent.md) | Read a publication without a version as describing the version last sent | proposed | 2026-10-13 |
| [0585](0585-pull-diagnostics-for-documents-shown-in-tabs-and-ask-their-colours-again-after-each-pull.md) | Pull diagnostics for documents shown in tabs and ask their colours again after each pull | proposed | 2026-10-13 |
| [0586](0586-treat-a-branch-review-as-a-session-with-a-fixed-base.md) | Treat a branch review as a session with a fixed base | proposed | 2026-10-14 |
| [0587](0587-show-a-changed-file-s-diff-since-the-version-that-was-viewed.md) | Show a changed file's diff since the version that was viewed | proposed | 2026-10-14 |
| [0588](0588-draw-checkboxes-with-a-tick-box-of-the-app-s-own.md) | Draw checkboxes with a tick box of the app's own | proposed | 2026-10-14 |
| [0589](0589-default-a-review-s-base-to-the-branch-tracking-origin-s-head.md) | Default a review's base to the branch tracking origin's HEAD | proposed | 2026-10-14 |
| [0590](0590-compile-the-bundled-fonts-into-the-executable.md) | Compile the bundled fonts into the executable | proposed | 2026-10-15 |
| [0591](0591-count-everyone-with-an-own-commit-as-a-branch-s-author.md) | Count everyone with an own commit as a branch's author | proposed | 2026-10-15 |
| [0592](0592-build-the-branch-list-as-rows-from-a-pure-function-and-draw-only-the-rows-on-screen.md) | Build the branch list as rows from a pure function and draw only the rows on screen | proposed | 2026-10-15 |
| [0598](0598-delete-only-the-untracked-files-a-deleted-folder-lists-in-the-changes-panel.md) | Delete only the untracked files a deleted folder lists in the changes panel | proposed | 2026-10-15 |
| [0599](0599-lay-out-the-buttons-of-every-modal-question-through-dialoglayout.md) | Lay out the buttons of every modal question through DialogLayout | proposed | 2026-10-16 |
| [0600](0600-keep-an-unreadable-state-file-as-bak-before-writing-over-it.md) | Keep an unreadable state file as .bak before writing over it | proposed | 2026-10-16 |
| [0601](0601-draw-every-field-that-narrows-a-list-through-drawfilterfield.md) | Draw every field that narrows a list through DrawFilterField | proposed | 2026-10-16 |
| [0602](0602-draw-every-button-that-is-only-a-glyph-through-core-drawiconbutton.md) | Draw every button that is only a glyph through Core::DrawIconButton | proposed | 2026-10-16 |
| [0603](0603-mark-whatever-opens-and-closes-in-a-list-with-the-one-dim-chevron.md) | Mark whatever opens and closes in a list with the one dim chevron | proposed | 2026-10-16 |
| [0604](0604-draw-every-list-row-through-listrowselectable.md) | Draw every list row through ListRowSelectable | proposed | 2026-10-16 |
| [0605](0605-draw-every-bar-over-the-editor-through-drawinfobar.md) | Draw every bar over the editor through DrawInfoBar | proposed | 2026-10-16 |
| [0606](0606-draw-every-line-number-column-through-linenumbercolumn.md) | Draw every line number column through LineNumberColumn | proposed | 2026-10-16 |
| [0607](0607-draw-the-middle-of-every-notification-through-one-piece-of-core-code.md) | Draw the middle of every notification through one piece of Core code | proposed | 2026-10-16 |
<!-- /adr-index -->
