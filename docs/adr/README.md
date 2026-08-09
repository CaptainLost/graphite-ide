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
<!-- /adr-index -->
