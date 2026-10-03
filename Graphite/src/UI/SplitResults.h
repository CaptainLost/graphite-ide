#pragma once

#include <functional>

namespace Graphite {

	// The stacked, splitter-divided layout used wherever a list sits above a detail pane: any
	// list of hits, commits or records whose one picked entry is shown in more detail draws it,
	// so the app's lists all open and divide their details the same way. Results on top, a
	// preview pane below once something is selected, with a draggable divider between them. A
	// preview the reader can dismiss closes through DrawPaneClose below — some, like search's
	// and references', have nothing to dismiss and never call it.
	// Stacked rather than side by side because what heads such a list — a query field and its
	// row of toggles, a summary line, a chart and the filters under it — runs the full width
	// above both, and a preview beside the results would make three columns of a panel that
	// reads top to bottom. A preview that wants columns of its own lays them out inside the
	// pane, where they have the list's whole width to share.
	//
	// Draws only the results, filling the whole space, when `HasSelection` is false: such a list
	// is read as a list far more often than one entry of it is looked at, and a pane reserving
	// half the height for "nothing selected" is half a panel spent saying nothing. The preview
	// then costs nothing until something is picked.
	struct SplitResultsInput
	{
		bool HasSelection = false;

		// Persisted by the caller across frames, so a split the user drags stays theirs until
		// they drag it again — including across a preview being dismissed and brought back.
		// Zero until the first frame that shows both, which is when there is a panel height to
		// split.
		float& ResultsHeight;

		const std::function<void()>& DrawResults;
		const std::function<void()>& DrawPreview;

		// Ids for the two children and the splitter between them. Distinct per caller so two of
		// these panels open at once do not fight over the same ImGui id, and so a split dragged
		// in one is remembered separately from the other's.
		const char* ResultsChildId = nullptr;
		const char* PreviewChildId = nullptr;
		const char* SplitterId = nullptr;
	};

	// What the results get the first time a preview appears: about two thirds of what the two
	// panes share once the divider's band is taken out. These panels dock at the bottom where
	// they are short, and half of one left the list about two rows; the preview is the pane
	// the reader looks at second, so it gives way. The caller's minimums still apply on top.
	inline constexpr float kDefaultResultsShare = 0.65f;

	float DefaultResultsHeight(float available, float divider);

	// Call with the content region this panel has to fill, after whatever heading the caller
	// draws above it (a query field, a summary line). Opens and closes both children itself.
	void DrawSplitResults(const SplitResultsInput& input);

	// The close in a preview pane's top-right corner, and whether it was pressed. Drawn at the
	// pane's right edge rather than after whatever precedes it, so a reader finds it in the
	// corner however many controls that pane puts on the row first — which is why it is called
	// last on the pane's opening line, and why it places itself instead of following a SameLine
	// the caller wrote.
	//
	// For any pane the reader can dismiss, such as a preview DrawSplitResults draws, so that
	// every such pane is closed from the same place. What closing means is the caller's
	// business; this only asks.
	bool DrawPaneClose();
}
