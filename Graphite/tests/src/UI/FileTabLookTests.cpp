#include <doctest/doctest.h>

#include "ImGuiFrames.h"

#include "UI/FileIcons.h"
#include "UI/FileTabLook.h"

#include <imgui.h>

#include <optional>

namespace {
	constexpr ImU32 kModifiedColour = IM_COL32(229, 192, 123, 255);

	Graphite::FileTab ReportTab()
	{
		return Graphite::FileTab{.Path = "C:/project/Report.md", .Title = "Report.md"};
	}
}

TEST_CASE("A file's tab leads with its kind's badge, as wide as a badge, before its title")
{
	ImGuiFrames frames;

	frames.Next([] {
		const Core::DockTabLook look = Graphite::FileTabLook(ReportTab(), nullptr);

		REQUIRE(look.Lead.has_value());
		CHECK(look.Lead->Width == doctest::Approx(Graphite::FileIconWidth()));
		CHECK(look.Glyph.empty());
		CHECK(look.Name == "Report.md");
	});
}

TEST_CASE("A preview tab's label is italic and a permanent one's upright")
{
	ImGuiFrames frames;

	frames.Next([] {
		ImFont* const italic = ImGui::GetFont();

		Graphite::FileTab preview = ReportTab();
		preview.Preview = true;
		CHECK(Graphite::FileTabLook(preview, italic).Face == italic);

		CHECK(Graphite::FileTabLook(ReportTab(), italic).Face == nullptr);
	});
}

TEST_CASE("A modified file's tab carries a dot in the modified colour, and a clean one none")
{
	ImGuiFrames frames;

	frames.Next([] {
		Graphite::FileTab modified = ReportTab();
		modified.Dot = kModifiedColour;

		CHECK(Graphite::FileTabLook(modified, nullptr).Dot == std::optional<ImU32>{kModifiedColour});
		CHECK_FALSE(Graphite::FileTabLook(ReportTab(), nullptr).Dot.has_value());
	});
}
