# 0195. ES.33: If you must use macros, give them unique names

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Macros do not obey scope rules. A macro is in force from its definition to the
end of the translation unit, across every header included after it, regardless
of namespace, class, or block. Uniqueness of the name is therefore the only
protection available against a collision.

```cpp
#define MYCHAR        /* BAD, will eventually clash with someone else's MYCHAR*/
#define ZCORP_CHAR    /* Still evil, but less likely to clash */
```

Avoid macros where you can:
[0192](0192-es-30-dont-use-macros-for-program-text-manipulation.md),
[0193](0193-es-31-dont-use-macros-for-constants-or-functions.md) and
[0194](0194-es-32-use-all-caps-for-all-macro-names.md) between them cover most of
what macros are used for. However, there are billions of lines of code littered
with macros, and a long tradition of using and overusing them. Where you are
forced to use one, use a long name and a supposedly unique prefix — an
organisation's or a component's name — to lower the likelihood of a clash.

The collision this prevents is not symmetric. Two functions with the same name
in different namespaces coexist; two macros with the same name do not, and the
second definition either wins silently or produces a redefinition warning that
appears in whichever file happened to include both. Neither party did anything
wrong, and the party that pays is usually the one that included them.

## Decision

Prefix every macro with something unlikely to be chosen by anyone else.

- Give every macro defined here a component or project prefix — the same prefix
  throughout — so that its name cannot plausibly appear in another library.
- Prefer a long, specific name to a short general one. `CHAR`, `MAX`, `ERROR`
  and `ASSERT` are all names other libraries have already taken.
- Combine this with ALL_CAPS per
  [0194](0194-es-32-use-all-caps-for-all-macro-names.md); the two rules describe
  the same name.
- `#undef` a macro at the end of a header when it exists only to build that
  header's contents, so its lifetime ends with its purpose.
- Where a third-party header defines an unprefixed macro that collides with
  something here, `#undef` it or rename the local entity at the include
  boundary; do not carry the ambiguity inwards.

## Rationale

Every other name in C++ has a scoping mechanism to fall back on — namespaces,
classes, blocks, internal linkage — and a macro has none. Uniqueness by
construction is therefore not one option among several for macros, it is the
only one. The prefix is admitted upstream to be a poor substitute for scope, and
it is: it makes a collision unlikely rather than impossible, which is the best
the preprocessor allows.

## Consequences

### Positive

- Collisions with other libraries' macros become unlikely rather than a matter
  of luck.
- A prefixed macro name identifies where it came from, which is the information
  a reader hunting an unexpected expansion needs first.
- `#undef` at the end of a header confines a macro to the file that needed it.

### Negative

- Prefixed macro names are long, and they appear at every use site, so the code
  that uses macros heavily gets noisier.
- A prefix reduces the probability of a collision without removing it; two
  components can still choose the same prefix, and nothing detects this until it
  breaks.
- The rule legitimises keeping macros by making them tolerable, which is in
  tension with
  [0192](0192-es-30-dont-use-macros-for-program-text-manipulation.md) and
  [0193](0193-es-31-dont-use-macros-for-constants-or-functions.md).
- It cannot be applied to macros arriving from third-party or platform headers,
  which is where most collisions actually originate.

### Neutral

- The rule applies only where a macro is genuinely necessary; the preceding
  rules decide whether it is.
- Which prefix to use is not settled here, only that there should be one and
  that it should be used consistently.

## Enforcement

Warn against short macro names.

## Related

- [0194](0194-es-32-use-all-caps-for-all-macro-names.md) — the other half of the
  naming rule for macros.
- [0192](0192-es-30-dont-use-macros-for-program-text-manipulation.md) — the rule
  that makes this one rarely necessary.
- [0193](0193-es-31-dont-use-macros-for-constants-or-functions.md) — likewise,
  for constants and function-like macros.
- [0182](0182-es-12-do-not-reuse-names-in-nested-scopes.md) — the scoping this
  rule exists precisely because macros do not have.
