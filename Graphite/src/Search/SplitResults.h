#pragma once

#include <functional>

namespace Graphite {

	// The stacked, splitter-divided layout the search panel and the references panel both draw:
	// results on top, a preview pane below once something is selected, with a draggable divider
	// between them. Stacked rather than side by side because the query field and its row of
	// toggles — or, for the references panel, the summary line — run the full width above both,
	// and a preview beside the results would make three columns of a panel that reads top to
	// bottom.
	//
	// Draws only the results, filling the whole space, when `HasSelection` is false: this panel
	// is used to read a list far more often than to look at one hit, and a pane reserving half
	// the height for "nothing selected" is half a panel spent saying nothing. The preview then
	// costs nothing until something is picked.
	struct SplitResultsInput
	{
		bool HasSelection = false;

		// Persisted by the caller across frames, so a split the user drags stays theirs until
		// they drag it again — including across a preview being dismissed and brought back.
		// Zero until the first frame that shows both, which is when there is a panel height to
		// halve.
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

	// Call with the content region this panel has to fill, after whatever heading the caller
	// draws above it (a query field, a summary line). Opens and closes both children itself.
	void DrawSplitResults(const SplitResultsInput& input);
}
