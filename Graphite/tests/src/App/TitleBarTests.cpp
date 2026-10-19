#include <doctest/doctest.h>

#include "App/TitleBar.h"
#include "App/Version.h"
#include "Core/ImGui/CaptionLayout.h"
#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/Theme.h"
#include "Theme/BuiltInSchemes.h"
#include "Theme/ChromePalette.h"

#include "DrawnColours.h"
#include "ImGuiFrames.h"

#include <imgui.h>

#include <cmath>
#include <optional>
#include <string>

using Graphite::TitleContext;

namespace {
	constexpr float kBarWidth = 1200.0f;
	constexpr float kMenusEnd = 300.0f;
	constexpr float kNarrowBarWidth = 520.0f;
	constexpr ImGuiWindowFlags kBareWindow = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
											 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
	constexpr ImVec2 kWindowSize(kBarWidth, 32.0f);

	struct Drawn
	{
		Core::CaptionLayout Layout;
		int Vertices = 0;
	};

	Drawn DrawTitleOnce(const TitleContext& context, float barWidth = kBarWidth)
	{
		ImGuiFrames frames;
		Drawn drawn;

		frames.Next([&] {
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(barWidth, kWindowSize.y));
			ImGui::Begin("##titleBarTest", nullptr, kBareWindow);

			const int before = ImGui::GetWindowDrawList()->VtxBuffer.Size;
			drawn.Layout = Graphite::DrawTitle(context, kMenusEnd, barWidth);
			drawn.Vertices = ImGui::GetWindowDrawList()->VtxBuffer.Size - before;

			ImGui::End();
		});

		return drawn;
	}
}

TEST_CASE("The title bar draws the folder and branch after the menus and no version")
{
	const Drawn none = DrawTitleOnce({});
	const Drawn folder = DrawTitleOnce({.Folder = "ab"});
	const Drawn longerFolder = DrawTitleOnce({.Folder = "abcd"});
	const Drawn both = DrawTitleOnce({.Folder = "ab", .Branch = "xy"});

	CHECK(folder.Layout.DividerVisible);
	CHECK(folder.Layout.FolderVisible);
	CHECK_FALSE(folder.Layout.BranchVisible);
	CHECK(both.Layout.BranchVisible);

	CHECK(folder.Layout.Divider.X >= kMenusEnd);
	CHECK(folder.Layout.Folder.X > folder.Layout.Divider.X);
	CHECK(both.Layout.Branch.X > both.Layout.Folder.X + both.Layout.Folder.Width);

	constexpr int kVerticesInAGlyph = 4;
	CHECK(longerFolder.Vertices - folder.Vertices == 2 * kVerticesInAGlyph);

	const int branchAndItsGlyph = both.Vertices - folder.Vertices;
	CHECK(branchAndItsGlyph == 3 * kVerticesInAGlyph);

	const std::string version = Graphite::Version::String();
	const int versionGlyphs = static_cast<int>(version.size()) + static_cast<int>(std::string("Graphite ").size());
	CHECK(folder.Vertices < none.Vertices + (versionGlyphs + 2) * kVerticesInAGlyph);
}

TEST_CASE("With no folder, nothing follows the menus")
{
	const Drawn drawn = DrawTitleOnce({.Branch = "main"});

	CHECK_FALSE(drawn.Layout.DividerVisible);
	CHECK_FALSE(drawn.Layout.FolderVisible);
	CHECK_FALSE(drawn.Layout.BranchVisible);
	CHECK(drawn.Vertices == 0);
}

TEST_CASE("The labels are dropped before they would overlap the window buttons")
{
	const TitleContext context{.Folder = "a folder with a rather long name", .Branch = "feature/a-branch-with-a-long-name"};

	const Drawn roomy = DrawTitleOnce(context);
	const Drawn narrow = DrawTitleOnce(context, kNarrowBarWidth);

	CHECK(roomy.Layout.BranchVisible);
	CHECK_FALSE(narrow.Layout.BranchVisible);
	CHECK(narrow.Vertices < roomy.Vertices);

	if (narrow.Layout.FolderVisible)
	{
		CHECK(narrow.Layout.Folder.X + narrow.Layout.Folder.Width <= narrow.Layout.Minimize.X);
	}
}

TEST_CASE("The title-bar divider is drawn in the line colour, in dark and in light")
{
	for (const Graphite::ResolvedScheme* scheme : {&Graphite::DarkScheme(), &Graphite::LightScheme()})
	{
		const Core::ChromePalette palette = Graphite::ChromePaletteFor(*scheme);
		CAPTURE(palette.Background.R);

		ImGuiFrames frames;
		Core::Theme::Apply(palette);

		Core::CaptionLayout layout;
		std::optional<HorizontalExtent> inLineColour;
		frames.Next([&] {
			// As the dock space host draws the title bar: with no border of its own, which is in the line colour too.
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(kWindowSize);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
			ImGui::Begin("##titleBarDividerTest", nullptr, kBareWindow);
			ImGui::PopStyleVar();

			layout = Graphite::DrawTitle({.Folder = "ab"}, kMenusEnd, kBarWidth);
			inLineColour = HorizontalExtentInColour(*ImGui::GetWindowDrawList(), ImGui::GetColorU32(Core::ToImVec4(palette.Border)));

			ImGui::End();
		});

		REQUIRE(layout.DividerVisible);
		REQUIRE(inLineColour.has_value());
		const float dividerLeft = std::floor(layout.Divider.X);
		CHECK(inLineColour->Left == doctest::Approx(dividerLeft));
		CHECK(inLineColour->Right == doctest::Approx(dividerLeft + layout.Divider.Width));
	}
}

TEST_CASE("The folder is named by the last part of its path")
{
	CHECK(Graphite::FolderLabel("D:/Repos/IDE") == "IDE");
	CHECK(Graphite::FolderLabel("D:/Repos/IDE/") == "IDE");
	CHECK(Graphite::FolderLabel("D:/") == "D:/");
}
