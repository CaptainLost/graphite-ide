# 0599. Lay out the buttons of every modal question through DialogLayout

Status: proposed
Date: 2026-10-16

## Context

Every modal question goes through `ModalQuestion`
([0475](0475-ask-every-modal-question-through-modalquestion.md)), but until now
each dialog drew its own buttons. About half used `DialogLayout::ButtonRow`; the
rest drew `ImGui::Button` rows by hand. The two kinds behaved differently in ways
a user notices without being able to name them:

- Escape cancelled a `ButtonRow` dialog and did nothing in a hand-drawn one,
  because ImGui never closes a modal on Escape by itself.
- Enter accepted the safe choice of a `ButtonRow` dialog and did nothing in a
  hand-drawn one, or activated whichever button ImGui focused first.
- The order of the buttons, their width and their colour differed, so a
  destructive button sat where another dialog put its safe one.

## Decision

Draw the buttons of every modal question with `DialogLayout::ButtonRow`, or with
`DialogLayout::AcknowledgeRow` when there is only one answer:

- Cancel comes first, and Escape answers it.
- The safe choice, if there is one, is filled in the accent colour, takes the
  default focus, and answers Enter.
- A destructive choice is filled in the danger colour and never answers Enter.
  A choice that loses work no history holds — deleting, discarding, overwriting,
  reloading over unsaved edits — is destructive.
- When cancelling is itself a decision, the Cancel slot carries that decision's
  label, as "Keep My Version" does when a file changed on disk under unsaved
  edits, so Escape makes it.
- A dialog with one answer, such as About or a refused key binding, answers it on
  click, Escape and Enter.

The text above the buttons is drawn with `DialogLayout::Text`, `MutedText` and
`Gap`, and the dialog is sized by `BeforeBegin`.

## Rationale

The keys are the part a user learns. With one row every dialog answers Escape and
Enter the same way, and a destructive choice is always a click away and red, so
the habit of pressing Enter can only ever make the safe choice.

## Consequences

### Positive

- Escape works in every dialog, and Enter never destroys anything.
- A new dialog gets the order, colours and keys by calling one function.

### Negative

- Dialogs that offered Enter on nothing now take it for their safe choice, such
  as Save when closing a tab, so a save that used to need a click takes a key.
  That save now asks before writing over a file another program changed.
- A hand-drawn dialog can no longer put its buttons in an order of its own.

### Neutral

- Escape on the reload question keeps your version, which refreshes the disk
  stamp, so the next save writes over the change on disk without asking again.

## Alternatives Considered

- **Keep hand-drawn rows and only add Escape.** Fixes the most visible gap, but
  leaves the order and colours different and each dialog repeating the key
  handling.
- **Make every choice a plain button and use no Enter at all.** Removes the risk
  of Enter, but also the fastest way through the common safe answers, such as
  loading a large file the user just asked to open.

## Related

- [0475](0475-ask-every-modal-question-through-modalquestion.md) — the questions
  these rows answer.
- [0515](0515-ask-before-every-discard-in-the-changes-panel.md) — a destructive
  question drawn this way.
