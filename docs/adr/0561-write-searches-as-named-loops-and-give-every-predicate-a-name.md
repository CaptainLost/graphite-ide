# 0561. Write searches as named loops and give every predicate a name

Status: accepted
Date: 2026-10-05

## Context

[0172](0172-es-1-prefer-the-standard-library-to-other-libraries-and-to-handcrafted-code.md),
[0365](0365-t-141-use-an-unnamed-lambda-if-you-need-a-simple-function-object-in-one-place-only.md)
and [0191](0191-es-28-use-lambdas-for-complex-initialization-especially-of-const-variables.md)
pointed the code towards standard algorithms with inline lambdas, unnamed
function objects and immediately invoked lambdas. To a reader fluent in the
standard library these are compact; to a junior engineer a line such as

```cpp
std::any_of(keys.begin(), keys.end(), [&prefix](const std::string& key) { return key.compare(0, prefix.size(), prefix) == 0; })
```

asks for three things at once — what `any_of` does, how the lambda captures,
and what three-argument `compare` returns — before the question it answers,
"does any key start with this prefix?", becomes visible. The survey behind
[0558](0558-write-code-a-junior-engineer-can-read-like-prose.md) found about
three hundred such constructs, among them lambdas used as local functions,
lambdas nested inside algorithms, a self-recursive generic lambda, and worker
jobs written as lambdas of 150 lines.

## Decision

**Searches, checks, counts and transformations are loops.** Replace `find_if`,
`any_of`, `all_of`, `none_of`, `count_if`, `transform`, `accumulate`, `copy_if`
and range pipelines with a `for` loop, usually inside a function whose name says
what it computes:

```cpp
bool AnyKeyStartsWith(const std::vector<std::string>& keys, const std::string& directoryPrefix)
{
	for (const std::string& key : keys)
	{
		if (key.starts_with(directoryPrefix))
		{
			return true;
		}
	}

	return false;
}
```

**Algorithms a loop would make worse stay**: `sort`, `stable_sort`,
`lower_bound`, `upper_bound`, `unique`, `erase_if`. A hand-written sort,
binary search or erase-while-iterating is longer and easier to get wrong. Their
comparator or predicate is a named function: `std::sort(entries.begin(), entries.end(), IsNameBefore)`.

**A lambda is allowed in two places only:**

- as a one-line adapter whose body calls a named function, where a capture is
  needed: `[&viewport](const Row& row) { return IsVisibleIn(row, viewport); }`;
- as a callback handed to an API — a job, a `std::function` seam, a C callback.
  A callback longer than a few lines forwards to a named function.

**Not allowed:** a lambda used as a local function, an immediately invoked
lambda (a named helper in the unnamed namespace replaces it), a lambda nested in
another, and a generic lambda.

## Rationale

A loop shows the whole computation — what is visited, what is tested, what is
returned — in the order it happens, using only the language's most basic
statements. A named function then gives the loop the one-line summary the
algorithm name was supposed to provide, but in the words of the problem
(`AnyKeyStartsWith`) rather than of the library (`any_of`). The algorithms that
stay are the ones whose loop form is a known source of bugs; naming their
predicate removes the part a junior engineer stumbles on.

## Consequences

### Positive

- The computation is visible without knowing the standard library's algorithm
  catalogue or lambda capture rules.
- Named predicates and helpers are reused instead of rewritten inline, and can
  be tested on their own.
- Lifetime bugs from captures shrink to the two places lambdas remain.

### Negative

- More, smaller functions in each file's unnamed namespace, and longer code
  than the one-line algorithm calls they replace.
- A helper that needs local state takes it as parameters, which is wordier than
  a capture.
- Some idioms experienced readers recognise instantly (`erase_if` with an inline
  predicate) are spelled out at more length.

### Neutral

- Hot paths keep their algorithm, as
  [0558](0558-write-code-a-junior-engineer-can-read-like-prose.md) exempts them;
  only names change there.

## Related

- [0558](0558-write-code-a-junior-engineer-can-read-like-prose.md) — the
  principle this applies.
- Supersedes [0191](0191-es-28-use-lambdas-for-complex-initialization-especially-of-const-variables.md)
  and [0365](0365-t-141-use-an-unnamed-lambda-if-you-need-a-simple-function-object-in-one-place-only.md).
- Amends [0172](0172-es-1-prefer-the-standard-library-to-other-libraries-and-to-handcrafted-code.md),
  [0468](0468-f-50-use-a-lambda-when-a-function-wont-do-to-capture-local-variables-or-to-write-a-local-function.md)
  and [0134](0134-c-170-if-you-feel-like-overloading-a-lambda-use-a-generic-lambda.md).
- [0438](0438-f-1-package-meaningful-operations-as-carefully-named-functions.md) —
  the named function is what a loop is packaged in.
