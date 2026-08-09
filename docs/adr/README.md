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
<!-- /adr-index -->
