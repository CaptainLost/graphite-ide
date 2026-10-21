#include <doctest/doctest.h>

#include "DrawnColours.h"
#include "HoveredItems.h"
#include "ImGuiFrames.h"

#include "Git/Diff/DiffView.h"

#include "Core/Git/Diff.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <optional>
#include <string_view>
#include <vector>

namespace {

	constexpr ImU32 kNoSelectionColour = 0;
	constexpr const char* kDiffWindowName = "Diff";

	bool DrawsHeaderText()
	{
		const ImU32 headerTextColour = ImGui::GetColorU32(ImGuiCol_TextDisabled);

		for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
		{
			if (std::string_view{window->Name}.starts_with(kDiffWindowName) &&
				DrawsInColour(*window->DrawList, headerTextColour))
			{
				return true;
			}
		}

		return false;
	}

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
		const Graphite::DiffViewOptions options{.Mode = mode};

		const Core::FileDiff file = HunksWithSignatureGapAndEmptySide();
		const Graphite::GitPalette palette;
		const Graphite::SyntaxPalette syntax;
		const Graphite::FileTokens tokens;

		int stackBefore = 0;
		int stackAfter = 0;
		bool headerTextDrawn = false;

		const auto draw = [&] {
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(900.0f, 400.0f));
			ImGui::Begin(kDiffWindowName, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);

			stackBefore = ImGui::GetCurrentContext()->FontStack.Size;
			view.Render(file, palette, syntax, kNoSelectionColour, nullptr, tokens, options);
			stackAfter = ImGui::GetCurrentContext()->FontStack.Size;
			headerTextDrawn = DrawsHeaderText();

			ImGui::End();
		};

		frames.Next(draw);
		frames.Next(draw);

		CHECK(stackAfter == stackBefore);
		CHECK(headerTextDrawn);
	}
}

namespace {

	constexpr ImVec2 kDiffWindowSize{900.0f, 400.0f};
	constexpr std::string_view kUnifiedChild = "/unified";
	constexpr std::string_view kSideBySideChild = "/pairs";

	bool DrewChild(std::string_view nameEnd)
	{
		const ImGuiContext& context = *ImGui::GetCurrentContext();

		for (const ImGuiWindow* window : context.Windows)
		{
			const std::string_view name{window->Name};

			if (name.starts_with(kDiffWindowName) && name.find(nameEnd) != std::string_view::npos &&
				window->LastFrameActive == context.FrameCount)
			{
				return true;
			}
		}

		return false;
	}

	struct DiffScreen
	{
		void Draw(const Graphite::DiffViewOptions& options)
		{
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(kDiffWindowSize);
			ImGui::Begin(kDiffWindowName, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);

			ToolbarY = ImGui::GetCursorScreenPos().y + ImGui::GetFrameHeight() * 0.5f;
			View.Render(File, Palette, Syntax, kNoSelectionColour, nullptr, Tokens, options);
			UnifiedButton = ImGui::GetCurrentWindow()->GetID("##unified");

			ImGui::End();
		}

		ImGuiFrames Frames;
		Graphite::DiffView View;
		const Core::FileDiff File = HunksWithSignatureGapAndEmptySide();
		const Graphite::GitPalette Palette;
		const Graphite::SyntaxPalette Syntax;
		const Graphite::FileTokens Tokens;

		float ToolbarY = 0.0f;
		ImGuiID UnifiedButton = 0;
	};
}

TEST_CASE("A diff is laid out as the options it is drawn with say, whichever diff chose them")
{
	DiffScreen screen;

	screen.Frames.Next([&] { screen.Draw(Graphite::DiffViewOptions{.Mode = Graphite::DiffViewMode::Unified}); });

	CHECK(DrewChild(kUnifiedChild));
	CHECK_FALSE(DrewChild(kSideBySideChild));

	screen.Frames.Next([&] { screen.Draw(Graphite::DiffViewOptions{.Mode = Graphite::DiffViewMode::SideBySide}); });

	CHECK(DrewChild(kSideBySideChild));
	CHECK_FALSE(DrewChild(kUnifiedChild));
}

TEST_CASE("A layout picked in a diff's toolbar is handed back as options for every diff and the rest are kept")
{
	DiffScreen screen;

	const Graphite::DiffViewOptions given{.Mode = Graphite::DiffViewMode::SideBySide,
										  .Scope = Graphite::DiffScope::WholeFile,
										  .ColourSyntax = false,
										  .WordWrap = true};

	std::optional<Graphite::DiffViewOptions> handedBack;
	screen.View.OnSetOptions = [&](const Graphite::DiffViewOptions& chosen) { handedBack = chosen; };

	const auto draw = [&] { screen.Draw(given); };
	screen.Frames.Next(draw);

	const std::vector<HoveredStretch> stretches = HoveredAcross(screen.Frames, screen.ToolbarY, 0.0f, kDiffWindowSize.x, draw);
	const HoveredStretch* unified = nullptr;

	for (const HoveredStretch& stretch : stretches)
	{
		if (stretch.Id == screen.UnifiedButton)
		{
			unified = &stretch;
		}
	}

	REQUIRE(unified != nullptr);
	screen.Frames.Click(ImVec2(unified->Middle(), screen.ToolbarY), draw);

	REQUIRE(handedBack.has_value());
	CHECK(handedBack->Mode == Graphite::DiffViewMode::Unified);
	CHECK(handedBack->Scope == given.Scope);
	CHECK(handedBack->ColourSyntax == given.ColourSyntax);
	CHECK(handedBack->WordWrap == given.WordWrap);
}
