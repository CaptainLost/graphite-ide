#include <doctest/doctest.h>

#include "Sessions/SessionIndex.h"

#include "TempDirectory.h"
#include "TranscriptFixtures.h"

#include <chrono>
#include <fstream>
#include <thread>

using namespace Graphite;

namespace {
	std::vector<SessionSummary> ScanAll(const std::filesystem::path& projects,
										const std::vector<SessionSummary>& cached = {})
	{
		std::vector<SessionSummary> found;
		ScanProjects(projects, cached, Core::CancellationToken{},
					 [&found](SessionSummary summary) { found.push_back(std::move(summary)); });
		return found;
	}

	// Drains the index until it is idle, or fails the test after five seconds.
	IndexDelivery Settle(SessionIndex& index)
	{
		IndexDelivery all;
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};

		while (true)
		{
			IndexDelivery delivery;
			index.Take(delivery);
			all.Updated.insert(all.Updated.end(), delivery.Updated.begin(), delivery.Updated.end());
			all.Removed.insert(all.Removed.end(), delivery.Removed.begin(), delivery.Removed.end());

			if (!index.IsScanning())
			{
				// A fresh delivery: Take accumulates into what it is given, and this one
				// already holds what the check above collected — reusing it here would count
				// that batch twice.
				IndexDelivery last;
				index.Take(last);
				all.Updated.insert(all.Updated.end(), last.Updated.begin(), last.Updated.end());
				all.Removed.insert(all.Removed.end(), last.Removed.begin(), last.Removed.end());
				return all;
			}

			REQUIRE(std::chrono::steady_clock::now() < deadline);
			std::this_thread::sleep_for(std::chrono::milliseconds{5});
		}
	}
}

TEST_CASE("Every session file is found, and subagent transcripts are not sessions")
{
	TempDirectory temp;
	temp.MakeFile("p1/a.jsonl", Fixtures::UserPrompt("a"));
	temp.MakeFile("p1/a/subagents/agent-1.jsonl", Fixtures::UserPrompt("sub"));
	temp.MakeFile("p2/b.jsonl", Fixtures::UserPrompt("b"));
	temp.MakeFile("p2/notes.txt", "not a transcript");

	CHECK(ScanAll(temp.GetPath()).size() == 2);
}

TEST_CASE("A cached summary whose fingerprint still matches is used as it is")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("p/a.jsonl", Fixtures::UserPrompt("a"));

	SessionSummary cached = SummariseSession(file);
	cached.Title = "from the cache";

	const std::vector<SessionSummary> found = ScanAll(temp.GetPath(), {cached});

	REQUIRE(found.size() == 1);
	CHECK(found[0].Title == "from the cache");
}

TEST_CASE("A cached summary whose file changed is summarised again")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("p/a.jsonl", Fixtures::UserPrompt("a"));

	SessionSummary cached = SummariseSession(file);
	cached.Title = "from the cache";

	{
		std::ofstream stream{file, std::ios::binary | std::ios::app};
		stream << Fixtures::AiTitle("fresh");
	}

	const std::vector<SessionSummary> found = ScanAll(temp.GetPath(), {cached});

	REQUIRE(found.size() == 1);
	CHECK(found[0].Title == "fresh");
}

TEST_CASE("A cancelled scan stops rather than finishing quietly")
{
	TempDirectory temp;
	for (int index = 0; index < 20; ++index)
		temp.MakeFile("p/s" + std::to_string(index) + ".jsonl", Fixtures::UserPrompt("x"));

	Core::CancellationToken token;
	std::size_t seen = 0;

	ScanProjects(temp.GetPath(), {}, token, [&](SessionSummary)
	{
		++seen;
		token.Cancel();
	});

	CHECK(seen == 1);
}

TEST_CASE("A change inside a session's own folder names that session")
{
	const std::filesystem::path projects = "C:\\claude\\projects";

	CHECK(SessionFileFor(projects, projects / "p" / "abc" / "subagents") == projects / "p" / "abc.jsonl");
	CHECK(SessionFileFor(projects, projects / "p" / "abc") == projects / "p" / "abc.jsonl");
	CHECK_FALSE(SessionFileFor(projects, projects / "p").has_value());
	CHECK_FALSE(SessionFileFor(projects, projects).has_value());
}

TEST_CASE("The index delivers every session, then what changed, then what was deleted")
{
	TempDirectory temp;
	const std::filesystem::path first = temp.MakeFile("p/a.jsonl", Fixtures::UserPrompt("a"));
	temp.MakeFile("p/b.jsonl", Fixtures::UserPrompt("b"));

	SessionIndex index;
	index.Start(temp.GetPath(), {});

	CHECK(Settle(index).Updated.size() == 2);

	{
		std::ofstream stream{first, std::ios::binary | std::ios::app};
		stream << Fixtures::AiTitle("changed");
	}

	index.Refresh({temp.GetPath() / "p"});
	const IndexDelivery changed = Settle(index);

	REQUIRE(changed.Updated.size() == 1);
	CHECK(changed.Updated[0].Title == "changed");

	std::filesystem::remove(first);
	index.Refresh({temp.GetPath() / "p"});
	const IndexDelivery removed = Settle(index);

	CHECK(removed.Updated.empty());
	REQUIRE(removed.Removed.size() == 1);
	CHECK(removed.Removed[0] == first);
}

TEST_CASE("A cached session whose transcript was deleted while the index was not running is delivered as removed")
{
	TempDirectory temp;
	const std::filesystem::path kept = temp.MakeFile("p/a.jsonl", Fixtures::UserPrompt("a"));
	const std::filesystem::path gone = temp.MakeFile("p/b.jsonl", Fixtures::UserPrompt("b"));

	const std::vector<SessionSummary> cached = {SummariseSession(kept), SummariseSession(gone)};
	std::filesystem::remove(gone);

	SessionIndex index;
	index.Start(temp.GetPath(), cached);
	const IndexDelivery delivery = Settle(index);

	REQUIRE(delivery.Updated.size() == 1);
	CHECK(delivery.Updated[0].File == kept);
	REQUIRE(delivery.Removed.size() == 1);
	CHECK(delivery.Removed[0] == gone);
}

TEST_CASE("A transcript that cannot be read is neither delivered nor remembered, so it is tried again")
{
	TempDirectory temp;
	temp.MakeFile("p/a.jsonl", Fixtures::UserPrompt("a"));

	// A directory where a transcript should be: it exists, and reading it fails.
	const std::filesystem::path unreadable = temp.MakeDir("p/x.jsonl");

	SessionIndex index;
	index.Start(temp.GetPath(), {});
	CHECK(Settle(index).Updated.size() == 1);

	index.Refresh({temp.GetPath() / "p" / "x"});
	CHECK(Settle(index).Updated.empty());

	// Had its fingerprint been kept, its disappearance would now be reported as a removal.
	std::filesystem::remove(unreadable);
	index.Refresh({temp.GetPath() / "p"});
	const IndexDelivery after = Settle(index);

	CHECK(after.Updated.empty());
	CHECK(after.Removed.empty());
}
