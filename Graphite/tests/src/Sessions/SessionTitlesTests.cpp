#include <doctest/doctest.h>

#include "Sessions/SessionTitles.h"

using namespace Graphite;

namespace {
	SessionSummary Session(const char* file, const wchar_t* cwd, const char* title, const char* endedAt)
	{
		SessionSummary summary;
		summary.Id = file;
		summary.File = std::filesystem::path{file};
		summary.Cwd = std::filesystem::path{cwd};
		summary.Title = title;
		summary.StartedAt = "2026-10-01T09:00:00.000Z";
		summary.EndedAt = endedAt;
		return summary;
	}

	std::map<std::filesystem::path, SessionSummary> Indexed(std::initializer_list<SessionSummary> summaries)
	{
		std::map<std::filesystem::path, SessionSummary> sessions;

		for (const SessionSummary& summary : summaries)
		{
			sessions[summary.File] = summary;
		}

		return sessions;
	}
}

TEST_CASE("The most recent session in a folder names it")
{
	const auto sessions = Indexed({
		Session("a.jsonl", L"D:/Repos/IDE", "Starsza praca", "2026-10-01T10:00:00.000Z"),
		Session("b.jsonl", L"D:/Repos/IDE", "Przebudowa panelu Git", "2026-10-02T10:00:00.000Z"),
	});

	const SessionTitles titles = LatestSessionTitles(sessions);

	CHECK(SessionTitleFor(titles, L"D:/Repos/IDE") == std::optional<std::string>{"Przebudowa panelu Git"});
}

TEST_CASE("An empty session does not name its folder")
{
	SessionSummary empty;
	empty.File = "e.jsonl";
	empty.Cwd = L"D:/Repos/IDE";
	empty.EndedAt = "2026-10-03T10:00:00.000Z";

	auto sessions = Indexed({Session("a.jsonl", L"D:/Repos/IDE", "Prawdziwa praca", "2026-10-01T10:00:00.000Z")});
	sessions[empty.File] = empty;

	CHECK(SessionTitleFor(LatestSessionTitles(sessions), L"D:/Repos/IDE") == std::optional<std::string>{"Prawdziwa praca"});
}

TEST_CASE("A folder is found whatever the case of its letters and its separators")
{
	const auto sessions = Indexed({Session("a.jsonl", L"D:/Repos/IDE/.claude/worktrees/x", "Tytul", "2026-10-01T10:00:00.000Z")});

	CHECK(SessionTitleFor(LatestSessionTitles(sessions), L"d:\\repos\\ide\\.claude\\worktrees\\X") ==
		  std::optional<std::string>{"Tytul"});
}

TEST_CASE("A folder no session ran in has no title")
{
	const auto sessions = Indexed({Session("a.jsonl", L"D:/Repos/IDE", "Tytul", "2026-10-01T10:00:00.000Z")});

	CHECK_FALSE(SessionTitleFor(LatestSessionTitles(sessions), L"D:/Repos/Other").has_value());
}
