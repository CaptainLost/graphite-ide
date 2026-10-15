#include <doctest/doctest.h>

#include "GitFixtures.h"
#include "ImGuiFrames.h"
#include "Git/History/CommitListView.h"

#include "Core/Git/GraphLayout.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <utility>

namespace {

	constexpr long long kNowSeconds = 1700000000;
	constexpr long long kSecondsBetweenCommits = 60;
	constexpr float kListWidth = 900.0f;
	constexpr float kListHeight = 400.0f;
	constexpr float kFarBelowTheEnd = 1.0e9f;
	constexpr int kFramesToSettle = 3;
	constexpr std::size_t kFirstPageSize = 500;
	constexpr std::size_t kSecondPageSize = 1000;
	constexpr std::uint8_t kMainHistory = 1;
	constexpr std::uint8_t kOtherHistory = 2;

	// A straight line of commits, newest first, cut off before its root as a first page is.
	Core::LogPage UnfinishedPage(std::size_t count, std::uint8_t history)
	{
		Core::LogPage page;
		page.Complete = false;

		for (std::size_t index = 0; index < count; ++index)
		{
			Core::CommitSummary commit;
			commit.Id.Bytes[0] = history;
			commit.Id.Bytes[1] = static_cast<std::uint8_t>(index & 0xFF);
			commit.Id.Bytes[2] = static_cast<std::uint8_t>((index >> 8) & 0xFF);
			commit.Summary = std::format("Commit {}", index);

			const long long seconds = kNowSeconds - static_cast<long long>(index) * kSecondsBetweenCommits;
			commit.Author = Core::Signature{.Name = "Ann", .Email = "ann@example.com", .When = SystemSecondsAt(seconds)};
			commit.Committer = commit.Author;

			page.Commits.push_back(std::move(commit));
		}

		for (std::size_t index = 0; index + 1 < count; ++index)
		{
			page.Commits[index].Parents.push_back(page.Commits[index + 1].Id);
		}

		return page;
	}

	enum class Scroll
	{
		Stay,
		ToTheEnd
	};

	void DrawList(Graphite::CommitListView& view, const Core::LogPage& page, Scroll scroll)
	{
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(kListWidth, kListHeight));
		ImGui::Begin("CommitList", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
												ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);

		const Core::CommitGraph graph = Core::LayoutCommits(page.Commits);

		view.Render(page, graph, Graphite::GitPalette{}, std::chrono::minutes{0},
					[](const Core::Oid&) { return Core::Ref<const Core::Diff>{}; },
					[](const Core::Oid&) { return false; }, Graphite::CommitRefMap{});

		// The scroll lands on the next frame, clamped to the end of the list, as a wheel flung
		// past the last row does.
		if (scroll == Scroll::ToTheEnd)
		{
			ImGuiTable* table = ImGui::TableFindByID(ImGui::GetID("##commits"));
			REQUIRE(table != nullptr);
			ImGui::SetScrollY(table->InnerWindow, kFarBelowTheEnd);
		}

		ImGui::End();
	}

	void Settle(ImGuiFrames& frames, Graphite::CommitListView& view, const Core::LogPage& page)
	{
		for (int frame = 0; frame < kFramesToSettle; ++frame)
		{
			frames.Next([&]() { DrawList(view, page, Scroll::Stay); });
		}
	}

	void ScrollToTheEnd(ImGuiFrames& frames, Graphite::CommitListView& view, const Core::LogPage& page)
	{
		frames.Next([&]() { DrawList(view, page, Scroll::ToTheEnd); });
		Settle(frames, view, page);
	}
}

TEST_CASE("A page shown from its top does not ask for more before the reader has scrolled")
{
	ImGuiFrames frames;
	Graphite::CommitListView view;
	const Core::LogPage page = UnfinishedPage(kFirstPageSize, kMainHistory);

	Settle(frames, view, page);

	CHECK_FALSE(view.TakeLoadMoreRequest());
}

TEST_CASE("Scrolling to the end of an unfinished page asks for more")
{
	ImGuiFrames frames;
	Graphite::CommitListView view;
	const Core::LogPage page = UnfinishedPage(kFirstPageSize, kMainHistory);

	Settle(frames, view, page);
	ScrollToTheEnd(frames, view, page);

	CHECK(view.TakeLoadMoreRequest());
}

TEST_CASE("A reader who stays at the end while the bigger page is fetched asks for it once")
{
	ImGuiFrames frames;
	Graphite::CommitListView view;
	const Core::LogPage page = UnfinishedPage(kFirstPageSize, kMainHistory);

	Settle(frames, view, page);
	ScrollToTheEnd(frames, view, page);
	REQUIRE(view.TakeLoadMoreRequest());

	Settle(frames, view, page);

	CHECK_FALSE(view.TakeLoadMoreRequest());
}

TEST_CASE("A new query's history as long as the page that last asked for more asks again at its end")
{
	ImGuiFrames frames;
	Graphite::CommitListView view;
	const Core::LogPage firstPage = UnfinishedPage(kFirstPageSize, kMainHistory);
	const Core::LogPage secondPage = UnfinishedPage(kSecondPageSize, kMainHistory);
	const Core::LogPage otherHistory = UnfinishedPage(kFirstPageSize, kOtherHistory);

	Settle(frames, view, firstPage);
	ScrollToTheEnd(frames, view, firstPage);
	REQUIRE(view.TakeLoadMoreRequest());

	Settle(frames, view, secondPage);
	view.TakeLoadMoreRequest();

	// As GitView does when it asks for another branch or filter from the first page.
	view.ForgetLoadMoreRequest();

	Settle(frames, view, otherHistory);
	ScrollToTheEnd(frames, view, otherHistory);

	CHECK(view.TakeLoadMoreRequest());
}
