# 0579. Offer Open beside a found colour and scroll its page to it

Status: accepted
Date: 2026-10-13

## Context

[0574](0574-list-what-a-settings-block-holds-as-rows-of-its-own-in-the-search.md) drew a found row with a control of its own, such as a colour's swatch, with that control alone, and gave Open only to a row without one. A colour of a built-in scheme cannot be edited, so its swatch is only for looking at: searching "string" with the built-in dark scheme showed a swatch that did nothing and no way on to the page. Nothing in the results said why.

## Decision

- **A found row gets Open when it has no control of its own, or when it sets `OpenWith`.** A row that does both is drawn with its control and Open after it.
- **A found colour sets `OpenWith` to its group's heading and its name**, such as `Syntax colours/String`. A name alone is not enough: Text and Background are in more than one group.
- **Opening the Appearance page with a colour's words scrolls the page so the colour is in the middle**, and lights its cell for a second and a half. Words that name no colour, such as the query an "N more matches" row hands on, open the page where it was left, as every page keeps its own scroll.
- **A block whose content must scroll the page sets `SettingsRow::DrawnEvenOutOfSight`** for as long as the scroll takes. ImGui skips the content of a block scrolled out of sight, so a colour in it could not otherwise scroll the page to itself.
- **A found colour of a built-in scheme says under its name how to change it**: "Built-in scheme: duplicate it to change this colour." A colour of the user's own scheme says nothing, as its swatch works. In the results an overridden colour's reset icon stands left of its swatch, so the swatches and the Open buttons line up without a gap where an inherited colour has no icon.
- Every other row with a control of its own keeps that control alone.

## Rationale

- **The way to change a colour of a built-in scheme is on its page, not in the results.** Open goes there, and the line under the name says what to do once there.
- **A toggle or a choice in the results is the whole setting.** There is nothing more beside it on its page, so Open would only take up room.

## Consequences

### Positive

- A colour found in the search can be reached on its page in one click, in the scheme that cannot be edited as well as in one that can.
- A new kind of found row that wants both its control and Open sets `OpenWith`, and nothing else changes.

### Negative

- **The scroll is asked for on two frames, not one.** A block drawn for the first time is measured only at the end of its frame, so a colour below it is placed right only on the next.
- **For those two frames the block holding the colour gives up ImGui's clipping** (`ImGuiChildFlags_AlwaysAutoResize`): it is drawn in full even where it cannot be seen.
- **Every found colour of a built-in scheme repeats the same sentence.** Eight rows of a query that matches many colours say it eight times.

### Neutral

- The swatch in the results still edits the colour of an own scheme in place, without opening the page.

## Alternatives Considered

- **Give every found row with a control an Open button.** Rejected: a toggle in the results already is the setting, and Open beside a wide control would leave the control less room.
- **Ask, on a click of a built-in scheme's swatch, whether to duplicate the scheme and edit the copy.** Rejected for now: it adds a dialog to a row in the results to save one click on the page, where the Duplicate button already is.

## Related

- [0574](0574-list-what-a-settings-block-holds-as-rows-of-its-own-in-the-search.md): refines its sentence "A found row with a control, such as a colour's swatch, is drawn with that control and works in the results".
