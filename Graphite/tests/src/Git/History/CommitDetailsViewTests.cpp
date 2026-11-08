#include <doctest/doctest.h>

#include "Git/History/CommitDetailsView.h"

#include "DrawnText.h"
#include "ImGuiFrames.h"

#include "Core/Git/Commit.h"
#include "Core/ImGui/FontSet.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace {
	constexpr int kFramesToSettle = 3;

	struct Extent
	{
		float Top = FLT_MAX;
		float Bottom = -FLT_MAX;
	};

	// The pane as it was drawn in its last frame.
	struct DrawnPane
	{
		float Height = 0.0f;
		float LineHeight = 0.0f;
		std::vector<DrawnLine> Lines;
		std::optional<Extent> TagPill;   // where the palette's RefTagFill is drawn
		std::optional<Extent> LocalPill; // where its RefLocalFill is drawn
	};

	std::optional<Extent> ExtentInColour(const ImDrawList& drawing, ImU32 colour)
	{
		Extent extent;

		for (const ImDrawVert& vertex : drawing.VtxBuffer)
		{
			if (vertex.col == colour)
			{
				extent.Top = std::min(extent.Top, vertex.pos.y);
				extent.Bottom = std::max(extent.Bottom, vertex.pos.y);
			}
		}

		return extent.Top <= extent.Bottom ? std::optional<Extent>{extent} : std::nullopt;
	}

	// The pane drawn for `content`, in a window of its own so no other test's state is in the way.
	DrawnPane DrawPane(const char* windowName, const Graphite::CommitDetailsContent& content,
					   const Graphite::GitPalette& palette = {})
	{
		Core::CommitSummary commit;
		commit.Id = *Core::Oid::FromHex(std::string(40, '1'));
		commit.Summary = "a commit";
		commit.Parents = {*Core::Oid::FromHex(std::string(40, '2'))};

		ImGuiFrames frames;
		DrawnPane pane;

		ImGui::GetStyle().FontSizeBase = Core::kInterfaceFontSize;

		for (int frame = 0; frame < kFramesToSettle; ++frame)
		{
			frames.Next([&] {
				ImGui::SetNextWindowSize(ImVec2(600.0f, 400.0f));
				ImGui::Begin(windowName);
				Graphite::RenderCommitDetails(commit, std::nullopt, false, nullptr, palette, content);
				pane.Height = ImGui::GetCursorPosY();
				pane.LineHeight = ImGui::GetTextLineHeight();

				const ImDrawList& drawing = *ImGui::GetWindowDrawList();
				pane.Lines = DrawnTextLines(drawing, *ImGui::GetFontBaked());
				pane.TagPill = palette.RefTagFill != 0 ? ExtentInColour(drawing, palette.RefTagFill) : std::nullopt;
				pane.LocalPill = palette.RefLocalFill != 0 ? ExtentInColour(drawing, palette.RefLocalFill) : std::nullopt;
				ImGui::End();
			});
		}

		return pane;
	}

	Graphite::CommitDetailsContent ContentWith(Graphite::FirstTagLine line)
	{
		Graphite::CommitDetailsContent content;
		content.FirstTag = std::move(line);
		return content;
	}

	std::chrono::sys_seconds ThreeDaysAgo()
	{
		return std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()) - std::chrono::hours{72};
	}
}

TEST_CASE("Commit details view: the First tag line has the same height pending, named and empty")
{
	const float withoutTags = DrawPane("FirstTagHeightNone", ContentWith({})).Height;
	const float pending = DrawPane("FirstTagHeightPending", ContentWith({.Shown = true, .Pending = true})).Height;
	const float named = DrawPane("FirstTagHeightNamed", ContentWith({.Shown = true, .Name = "v1.2", .When = ThreeDaysAgo()})).Height;
	const float empty = DrawPane("FirstTagHeightEmpty", ContentWith({.Shown = true})).Height;

	// The line is there when the repository has tags, and not otherwise.
	CHECK(pending > withoutTags);

	CHECK(named == doctest::Approx(pending));
	CHECK(empty == doctest::Approx(pending));
}

TEST_CASE("Commit details view: the First tag line comes right after In bases and the source sentence, before the refs")
{
	Graphite::GitPalette palette;
	palette.RefTagFill = IM_COL32(210, 150, 30, 255);
	palette.RefTagText = IM_COL32(20, 20, 20, 255);
	palette.RefLocalFill = IM_COL32(40, 90, 210, 255);
	palette.RefLocalText = IM_COL32(250, 250, 250, 255);

	Graphite::CommitDetailsContent content = ContentWith({.Shown = true, .Name = "v1.2", .When = ThreeDaysAgo()});
	content.Bases.Holding = {"main"};
	content.Source = "Squash of feature/x.";
	content.Refs = {Graphite::CommitRef{.Name = "topic", .Kind = Graphite::CommitRefKind::Local}};

	const DrawnPane pane = DrawPane("FirstTagPlace", content, palette);

	const std::optional<std::size_t> inBases = LineStartingWith(pane.Lines, "Inbases");
	const std::optional<std::size_t> source = LineStartingWith(pane.Lines, "Squashoffeature/x.");
	const std::optional<std::size_t> firstTag = LineStartingWith(pane.Lines, "Firsttag");
	const std::optional<std::size_t> parents = LineStartingWith(pane.Lines, "parents");
	REQUIRE(inBases.has_value());
	REQUIRE(source.has_value());
	REQUIRE(firstTag.has_value());
	REQUIRE(parents.has_value());

	CHECK(*source == *inBases + 1);
	CHECK(*firstTag == *source + 1);
	CHECK(pane.Lines[*firstTag].Text.ends_with("made3dago"));

	// The tag's pill is on First tag's line, and the refs line, its pills in their own smaller face, follows it.
	REQUIRE(pane.TagPill.has_value());
	REQUIRE(pane.LocalPill.has_value());
	CHECK(pane.TagPill->Top >= pane.Lines[*source].Top + pane.LineHeight * 0.5f);
	CHECK(pane.LocalPill->Top > pane.TagPill->Bottom);
	CHECK(pane.LocalPill->Bottom <= pane.Lines[*parents].Top);
}
