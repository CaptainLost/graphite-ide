#include <doctest/doctest.h>

#include "Git/History/CommitDetailsView.h"

#include "ImGuiFrames.h"

#include "Core/Git/Commit.h"
#include "Core/ImGui/FontSet.h"

#include <imgui.h>

#include <chrono>
#include <string>

namespace {
	constexpr int kFramesToSettle = 3;

	// The height of the pane drawn for `content`, in a window of its own so no other test's state is in the way.
	float PaneHeightOf(const char* windowName, const Graphite::CommitDetailsContent& content)
	{
		Core::CommitSummary commit;
		commit.Id = *Core::Oid::FromHex(std::string(40, '1'));
		commit.Summary = "a commit";
		commit.Parents = {*Core::Oid::FromHex(std::string(40, '2'))};

		const Graphite::GitPalette palette;
		ImGuiFrames frames;
		float height = 0.0f;

		ImGui::GetStyle().FontSizeBase = Core::kInterfaceFontSize;

		for (int frame = 0; frame < kFramesToSettle; ++frame)
		{
			frames.Next([&] {
				ImGui::SetNextWindowSize(ImVec2(600.0f, 400.0f));
				ImGui::Begin(windowName);
				Graphite::RenderCommitDetails(commit, std::nullopt, false, nullptr, palette, content);
				height = ImGui::GetCursorPosY();
				ImGui::End();
			});
		}

		return height;
	}

	Graphite::CommitDetailsContent ContentWith(Graphite::FirstTagLine line)
	{
		Graphite::CommitDetailsContent content;
		content.FirstTag = std::move(line);
		return content;
	}
}

TEST_CASE("Commit details view: the First tag line has the same height pending, named and empty")
{
	const std::chrono::sys_seconds made =
		std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()) - std::chrono::hours{72};

	const float withoutTags = PaneHeightOf("FirstTagHeightNone", ContentWith({}));
	const float pending = PaneHeightOf("FirstTagHeightPending", ContentWith({.Shown = true, .Pending = true}));
	const float named = PaneHeightOf("FirstTagHeightNamed", ContentWith({.Shown = true, .Name = "v1.2", .When = made}));
	const float empty = PaneHeightOf("FirstTagHeightEmpty", ContentWith({.Shown = true}));

	// The line is there when the repository has tags, and not otherwise.
	CHECK(pending > withoutTags);

	CHECK(named == doctest::Approx(pending));
	CHECK(empty == doctest::Approx(pending));
}
