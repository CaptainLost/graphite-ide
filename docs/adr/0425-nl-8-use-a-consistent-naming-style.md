# 0425. NL.8: Use a consistent naming style

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section NL (Naming and layout suggestions), transcribed 2026-08-10

## Context

Consistency in naming and naming style increases readability. This is the point
of the whole NL section: the stated aim is consistency rather than any
particular rule set.

There are many styles, and when several libraries are used, their conventions
cannot all be followed at once. The advice is to choose a "house style" and to
leave imported libraries with their original style. Three styles are given as
examples. The ISO Standard style uses lower case and digits only, separating
words with underscores:

```cpp
int
vector
my_map
```

Double underscores are to be avoided. Stroustrup's variant is the ISO style with
upper case for one's own types and concepts:

```cpp
int
vector
My_map
```

CamelCase capitalises each word in a multi-word identifier, with conventions
differing on whether the first letter is capitalised:

```cpp
int
vector
MyMap
myMap
```

Consistency is also asked for in the use of acronyms and in how much an
identifier spells out, because the alternative is a body of code that cannot
decide:

```cpp
int mtbf {12};
int mean_time_between_failures {12}; // make up your mind
```

Enforcement would be possible except for the use of libraries with varying
conventions.

## Decision

Pick one naming style and apply it to all the code you own.

- Choose a house style covering the cases that recur: types, functions,
  variables, data members, constants and namespaces. Write it down, because an
  unwritten convention is only enforceable by whoever remembers it.
- Apply it to code you write and maintain, and leave imported code in its
  original style rather than restyling it.
- Avoid double underscores anywhere in an identifier, and avoid a leading
  underscore followed by a capital letter; those spellings are reserved to the
  implementation.
- Be consistent about acronyms and about abbreviation. Choose `mtbf` or
  `mean_time_between_failures`, and use the same choice everywhere.
- Where a name must interoperate by spelling with an outside interface, that
  interface wins over the house style.

## Rationale

The value of a naming convention lies almost entirely in its uniformity and
almost none of it in the particular scheme, which is why the guidelines present
these as suggestions rather than rules. A consistent style lets a reader infer a
name's kind from its shape without looking it up, and that inference is only
reliable if it never fails — one exception costs more than the convention
gained.

## Consequences

### Positive

- A reader can tell a type from a function from a data member on sight.
- Naming stops being decided per identifier, which removes a small argument from
  every review.
- Writing the style down makes it survivable: it can be handed to a newcomer or
  to a tool.

### Negative

- No house style can cover a codebase that uses several libraries, so mixed
  conventions appear within single expressions and the eye never fully settles.
- A style chosen after code exists implies either a rename sweep, which is churn
  with no behavioural benefit, or a permanent split between old and new code.
- Enforcement is only partial. A checker must be scoped to exclude imported
  code and taught every prefix the style uses, and anything it cannot express
  holds by imitation alone.
- Any house style that is not the standard library's guarantees a permanent
  mismatch with the names used on nearly every line.

### Neutral

- Which style is best is not decided by this rule and the guidelines decline to
  decide it; a default is offered separately by
  [0427](0427-nl-10-prefer-underscore-style-names.md).
- The rule constrains spelling, not meaning. A consistently styled name can
  still be a bad name.

## Enforcement

Enforcement would be possible were it not for libraries with varying
conventions. Any checker has to be scoped to the code that follows the house
style.

## Related

- [0427](0427-nl-10-prefer-underscore-style-names.md) — the default style
  offered for when there is nothing already established.
- [0426](0426-nl-9-use-all-caps-for-macro-names-only.md) — the one naming rule
  in this section with a mechanical consequence rather than an aesthetic one.
- [0423](0423-nl-5-avoid-encoding-type-information-in-names.md) — what a name
  must not encode, whichever style is chosen.
- [0431](0431-nl-17-use-k-r-derived-layout.md) — the layout counterpart of the
  same house-style question.
