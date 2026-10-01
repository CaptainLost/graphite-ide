# 0536. Initialise a repository without asking for confirmation

Status: proposed
Date: 2026-10-01

## Context

When the open folder is not a git repository, the Changes and Git panels have
nothing to show. They now say so and offer an **Initialize Repository** button.
Whether that button should ask first depends on what the action can cost.

`git init` creates a `.git` directory at the root of the folder. It deletes and
overwrites nothing: run again in an existing repository it reinitialises
harmlessly. It runs nothing from the folder, so it is as safe in Restricted mode
as reading the file tree is. The undo is deleting `.git`. The first branch is
named from the user's git configuration (`init.defaultBranch`, `master` when
unset), so the user's own preference is already honoured.

## Decision

- Create the repository at the root of the open folder as soon as the button is
  clicked, with no confirmation dialog.
- Offer the button only while no repository was found for the folder, so it
  cannot be reached inside an existing one.
- Allow it in Restricted mode.
- Disable the button while another write is in flight; run the init as one more
  job on the write queue, so it is ordered with the rest and its success
  refreshes the status like any applied write.
- Show a failed init as the error in the prompt itself, under its sentence, in
  both panels.

## Rationale

A confirmation is worth its interruption when the action can lose something.
This one cannot, and the user has just pressed a button whose label says exactly
what it does. Asking would add a step to the common case to guard against a
cost - an unwanted `.git` directory - that is smaller than the dialog.

## Consequences

### Positive

- Starting version control on a folder is one click, and both panels and the
  status bar move to their normal state within a refresh.
- No new dialog, and nothing to test or localise beyond the prompt.

### Negative

- A misclick leaves a `.git` directory the user did not want and has to delete
  by hand; the IDE offers no undo for it.
- Initialising a folder that sits inside another repository's working tree is
  allowed by git, and nothing in the action warns about nesting. It is guarded
  only by the button appearing when no repository was found.

### Neutral

- The new repository starts with no commits and every file untracked, so a
  large folder shows a large changes count until a `.gitignore` is added.

## Alternatives Considered

- **Confirm first, naming the folder and the branch.** It protects against the
  misclick, at the price of a dialog in front of every use for an action that
  harms nothing. If init ever grows options, such as choosing the branch name or
  adding a `.gitignore`, a dialog earns its place then.
- **Create the repository silently when a folder is opened.** It removes the
  click, but it changes a folder the user did not ask to change and would make
  the status bar's `No repository` state unreachable.

## Related

- [0532](0532-open-an-unknown-folder-in-restricted-mode-until-the-user-trusts-it.md)
  - init is allowed in Restricted mode because it runs nothing from the folder.
- [0535](0535-show-git-state-in-the-status-bar-read-only-and-keep-git-actions-in-the-panels.md)
  - the bar reads `No repository` and stays inert; the action lives in the
  panels.
