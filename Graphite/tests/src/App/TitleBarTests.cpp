#include <doctest/doctest.h>

#include "App/TitleBar.h"
#include "App/Version.h"
#include "Core/ImGui/CaptionLayout.h"

#include "ImGuiFrames.h"

#include <imgui.h>

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

TEST_CASE("The folder is named by the last part of its path")
{
	CHECK(Graphite::FolderLabel("D:/Repos/IDE") == "IDE");
	CHECK(Graphite::FolderLabel("D:/Repos/IDE/") == "IDE");
	CHECK(Graphite::FolderLabel("D:/") == "D:/");
}
