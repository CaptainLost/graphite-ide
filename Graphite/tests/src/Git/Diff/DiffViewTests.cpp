#include <doctest/doctest.h>

#include "ImGuiFrames.h"

#include "Git/Diff/DiffView.h"

#include "Core/Git/Diff.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <vector>

namespace {

	constexpr bool kNoWordWrap = false;
	constexpr ImU32 kNoSelectionColour = 0;

	Core::DiffLine LineOf(Core::DiffLineKind kind, int oldLine, int newLine, const char* text)
	{
		return Core::DiffLine{.Kind = kind, .OldLine = oldLine, .NewLine = newLine, .Text = text};
	}

	Core::FileDiff HunksWithSignatureGapAndEmptySide()
	{
		Core::FileDiff file;
		file.OldPath = "widget.txt";
		file.NewPath = "widget.txt";
		file.Change = Core::FileChange::Modified;

		file.Hunks.push_back(Core::Hunk{.OldStart = 1,
										.OldCount = 2,
										.NewStart = 1,
										.NewCount = 2,
										.Header = "@@ -1,2 +1,2 @@ void Draw()",
										.Lines = {LineOf(Core::DiffLineKind::Context, 1, 1, "{\n"),
												  LineOf(Core::DiffLineKind::Removed, 2, 0, "\tOld();\n"),
												  LineOf(Core::DiffLineKind::Added, 0, 2, "\tNew();\n")}});

		file.Hunks.push_back(Core::Hunk{.OldStart = 20,
										.OldCount = 0,
										.NewStart = 21,
										.NewCount = 1,
										.Header = "@@ -20,0 +21,1 @@",
										.Lines = {LineOf(Core::DiffLineKind::Added, 0, 21, "\tMore();\n")}});

		return file;
	}
}

TEST_CASE("Hunk headers and signatures draw two frames with their fonts balanced in both layouts")
{
	for (const Graphite::DiffViewMode mode : {Graphite::DiffViewMode::Unified, Graphite::DiffViewMode::SideBySide})
	{
		CAPTURE(static_cast<int>(mode));

		ImGuiFrames frames;
		Graphite::DiffView view;
		view.SetMode(mode);

		const Core::FileDiff file = HunksWithSignatureGapAndEmptySide();
		const Graphite::GitPalette palette;
		const Graphite::SyntaxPalette syntax;
		const Graphite::FileTokens tokens;

		int stackBefore = 0;
		int stackAfter = 0;

		const auto draw = [&] {
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(900.0f, 400.0f));
			ImGui::Begin("Diff", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);

			stackBefore = ImGui::GetCurrentContext()->FontStack.Size;
			view.Render(file, palette, syntax, kNoSelectionColour, nullptr, tokens, kNoWordWrap);
			stackAfter = ImGui::GetCurrentContext()->FontStack.Size;

			ImGui::End();
		};

		frames.Next(draw);
		frames.Next(draw);

		CHECK(stackAfter == stackBefore);
	}
}
