#include "UI/SplitResults.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>

namespace Graphite {

	void DrawSplitResults(const SplitResultsInput& input)
	{
		if (!input.HasSelection)
		{
			if (ImGui::BeginChild(input.ResultsChildId, ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
								  ImGuiWindowFlags_HorizontalScrollbar))
			{
				input.DrawResults();
			}

			ImGui::EndChild();
			return;
		}

		const float available = ImGui::GetContentRegionAvail().y;

		// The thickness ImGui gives the resizing border between docked windows, read from the
		// same style field because this divider does the same job. At ItemSpacing.y, which is
		// what this first used, it was five pixels against the docking separators' two and read
		// as a heavier rule than anything else in the application.
		const float thickness = std::max(1.0f, ImGui::GetStyle().DockingSeparatorSize);

		// Air either side of the line, and the whole band is what the two panes have to share
		// the panel with. The line sat tight against both at first, like a docking separator —
		// but that separates panes with no frame of their own, and the preview has a border:
		// a box touching the divider reads as wedged against it.
		const float spacing = ImGui::GetStyle().ItemSpacing.y;
		const float divider = thickness + spacing * 2.0f;

		// A quarter of the panel each at the tightest, and two rows each when there is room for
		// more than that — scaled rather than fixed, because these panels dock at the bottom
		// where they are wide and short, and a fixed minimum taller than half of one would leave
		// the splitter nothing to travel through.
		const float minimum = std::min(ImGui::GetFrameHeight() * 2.0f, available * 0.25f);

		if (input.ResultsHeight <= 0.0f)
		{
			input.ResultsHeight = (available - divider) * 0.5f;
		}

		float previewHeight = available - divider - input.ResultsHeight;

		// The dock node resizes under us, which moves `available` without touching the split
		// the user dragged. Clamped here rather than rescaled: a split is a position the user
		// chose, and the pane that gives way when there is less room is the one they are not
		// looking at.
		if (previewHeight < minimum)
		{
			previewHeight = minimum;
			input.ResultsHeight = available - divider - previewHeight;
		}

		if (input.ResultsHeight < minimum)
		{
			input.ResultsHeight = minimum;
			previewHeight = available - divider - input.ResultsHeight;
		}

		if (ImGui::BeginChild(input.ResultsChildId, ImVec2(0.0f, input.ResultsHeight),
							  ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
		{
			input.DrawResults();
		}

		ImGui::EndChild();

		// A short delay before the divider lights up and the cursor changes, so sweeping the
		// mouse across the panel does not flash it. The same 0.04s ImGui waits before
		// highlighting a window's resize border (WINDOWS_RESIZE_FROM_EDGES_FEEDBACK_TIMER in
		// imgui.cpp) — copied as a value because that one is file-static.
		constexpr float kSplitterHoverDelay = 0.04f;

		// EndChild already left the cursor an item's spacing below the results, which is the
		// air above the line. The matching air below it is added after the bar.
		const ImVec2 at = ImGui::GetCursorScreenPos();
		const ImRect bar(at, ImVec2(at.x + ImGui::GetContentRegionAvail().x, at.y + thickness));

		// ImGui's own splitter rather than a line and a hand-rolled drag: it carries the
		// ResizeNS cursor, a grab zone wider than the line it draws, the minimum sizes above
		// and the Separator colours every other divider in the application uses. The grab zone
		// is the same padding ImGui gives a window's own border, so this divider is no harder
		// to catch than the ones around it.
		ImGui::SplitterBehavior(bar, ImGui::GetID(input.SplitterId), ImGuiAxis_Y,
								&input.ResultsHeight, &previewHeight, minimum, minimum,
								ImGui::GetCurrentContext()->WindowsBorderHoverPadding,
								kSplitterHoverDelay);

		// SplitterBehavior hit-tests and draws but never calls ItemSize, so the cursor is still
		// where the bar starts. Moved past the bar and its lower air in one step: a Dummy would
		// claim the bar's height and then have ImGui add the spacing, which is the same sum by
		// a longer route and leaves the bar's height in the layout for the next item to trip on.
		ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + thickness + spacing));

		// AlwaysUseWindowPadding, because a child's padding is otherwise conditional: ImGui
		// zeroes it for any bordered-looking child whose border size resolves to zero, and the
		// first line of the preview then sat against the divider with its top clipped. The
		// padding is wanted here whatever the border turns out to be, so it is asked for.
		if (ImGui::BeginChild(input.PreviewChildId, ImVec2(0.0f, 0.0f),
							  ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
							  ImGuiWindowFlags_HorizontalScrollbar))
		{
			input.DrawPreview();
		}

		ImGui::EndChild();
	}
}
