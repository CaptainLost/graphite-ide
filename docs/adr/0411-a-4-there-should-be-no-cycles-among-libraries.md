# 0411. A.4: There should be no cycles among libraries

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section A (Architectural ideas), transcribed 2026-08-10

## Context

A cycle among libraries complicates the build process, because there is no
order in which the participants can be built that satisfies all of them.
Cycles are also hard to understand, and they might introduce indeterminism —
unspecified behaviour — since nothing fixes which side of the cycle is
initialised, resolved or linked first.

The rule constrains libraries, not the contents of a library. A library may
contain cyclic references in the definitions of its components: two classes in
the same library may refer to each other, and that is ordinary design. What is
forbidden is a library depending on another library that depends on it, in
either direction, directly or through a chain of intermediates.

## Decision

Keep the dependency graph between libraries acyclic.

- Before adding a dependency from one library to another, check that the target
  does not already depend on the source, transitively.
- Where two libraries appear to need each other, resolve it rather than accept
  it: move the shared part into a third library that both depend on, invert one
  direction with an abstract interface or a callback owned by the lower library,
  or merge the two if they were never really separate.
- Do not resolve a cycle by widening a library's interface so that the
  dependency becomes implicit; a dependency that is real but undeclared is
  worse than one that is visible.
- Cycles inside a single library are permitted. Two classes in the same library
  may refer to each other.
- Decide the direction of each dependency once, deliberately, and treat any
  later edge that would run against it as a defect rather than as a convenience.

## Rationale

The build argument is the decisive one because it is mechanical: a cyclic
dependency between separately built artefacts has no valid build order, so the
cycle must be broken by something — a forward declaration, a hand-managed link
order, a manual two-pass build — and whatever breaks it becomes a permanent
piece of fragile scaffolding.

The comprehension argument matters more in the long run. An acyclic graph can be
read in layers, and a component understood without reading everything above it;
a cycle means the whole cluster is one unit that must be understood at once, and
it will grow, because nothing inside a cycle can be extracted from it.

## Consequences

### Positive

- The build has a valid topological order, so libraries can be built, and
  rebuilt, independently.
- Any single library can be read, tested and reasoned about knowing only what is
  below it in the graph.
- A library at the bottom of the graph can be reused elsewhere, because its
  dependency closure is finite and known.

### Negative

- Breaking a cycle costs an abstraction: an interface, an inversion, or a third
  library that exists only to be depended on by both. That is real code with no
  behaviour of its own.
- The rule pushes towards more, smaller libraries, which is its own cost in
  build configuration and navigation.
- It is stated as a design principle with no enforcement, and nothing in an
  ordinary build detects a cycle that a linker happens to tolerate.
- Where a genuine two-way relationship exists in the domain, the acyclic
  arrangement can be less direct than the cyclic one it replaces.

### Neutral

- Cyclic references within a library are explicitly permitted, so the rule is
  about packaging, not about mutual reference as such.
- Upstream illustrates the intra-library case with a placeholder rather than an
  example, so no code is transcribed here.

## Related

- [0410](0410-a-2-express-potentially-reusable-parts-as-a-library.md) — what a
  library is, which this rule constrains the relations between.
- [0409](0409-a-1-separate-stable-code-from-less-stable-code.md) — a cycle
  destroys the separation that rule asks for.
- [0036](0036-i-25-prefer-empty-abstract-classes-as-interfaces-to-class-hierarchies.md)
  — an abstract interface is how a dependency is inverted to break a cycle.
