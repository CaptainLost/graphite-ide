#include <doctest/doctest.h>

#include "Git/Lines/LineAnswerChange.h"

#include "Git/Lines/LineReader.h"
#include "Git/Lines/LineStore.h"

#include "GitFixture.h"
#include "TempDirectory.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

using namespace Graphite;

namespace {
	constexpr std::int64_t kNow = 1800000000;

	LineAnswerChange ChangeOf(LineAnswerChange::Kind what, std::string ref, std::string target = {})
	{
		return LineAnswerChange{.What = what, .Ref = std::move(ref), .Target = std::move(target)};
	}

	// staging added after release was kept, release's place in the order first; develop removed; the main line answered
	// as release; topic compared with staging.
	Core::LineAnswers SomeAnswers()
	{
		Core::LineAnswers answers;
		answers.AddedBases = {"refs/heads/staging"};
		answers.RemovedBases = {"refs/remotes/origin/develop"};
		answers.KeptBases = {"refs/remotes/origin/release"};
		answers.BaseOrder = {"refs/remotes/origin/release", "refs/heads/staging"};
		answers.MainLine = "refs/remotes/origin/release";
		answers.Comparisons["refs/heads/topic"] = "refs/heads/staging";
		return answers;
	}

	// master pushed to origin as its default, and a local feature with a commit of its own.
	void BuildMasterAndFeature(GitFixture& fixture)
	{
		fixture.Write("a.txt", "1");
		const std::string master = fixture.Commit("one");
		fixture.PushTo("master", master);
		fixture.SetRemoteHead("master");

		fixture.Branch("feature");
		fixture.Checkout("feature");
		fixture.Write("b.txt", "f");
		fixture.Commit("feature work");
	}

	LineStorePaths StoreIn(const GitFixture& fixture, const TempDirectory& directory)
	{
		return LineStoreFor(fixture.GetPath() / ".git", directory.GetPath() / "store");
	}

	void WriteText(const std::filesystem::path& file, const std::string& text)
	{
		std::filesystem::create_directories(file.parent_path());
		std::ofstream out{file, std::ios::binary | std::ios::trunc};
		out << text;
	}

	std::string ReadWhole(const std::filesystem::path& file)
	{
		std::ifstream stream{file, std::ios::binary};
		return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
	}

	bool HasBase(const LineRead& read, const std::string& fullName, Core::BaseReason reason)
	{
		return std::ranges::any_of(read.Model.Bases, [&read, &fullName, reason](const Core::LineBase& base) {
			return read.Refs[base.Ref].FullName == fullName && base.Reason == reason;
		});
	}
}

TEST_CASE("Git window: each line change edits the answers as described")
{
	Core::LineAnswers answers = SomeAnswers();
	Core::LineAnswers expected = SomeAnswers();

	SUBCASE("use as base takes a removed base back and gives it the next place")
	{
		ApplyLineChange(answers, ChangeOf(LineAnswerChange::Kind::UseAsBase, "refs/remotes/origin/develop"));

		expected.RemovedBases.clear();
		expected.AddedBases = {"refs/heads/staging", "refs/remotes/origin/develop"};
		expected.BaseOrder = {"refs/remotes/origin/release", "refs/heads/staging", "refs/remotes/origin/develop"};
	}

	SUBCASE("use as base names an added base once")
	{
		ApplyLineChange(answers, ChangeOf(LineAnswerChange::Kind::UseAsBase, "refs/heads/staging"));
	}

	SUBCASE("use as base keeps a kept base's place")
	{
		ApplyLineChange(answers, ChangeOf(LineAnswerChange::Kind::UseAsBase, "refs/remotes/origin/release"));

		expected.AddedBases = {"refs/heads/staging", "refs/remotes/origin/release"};
	}

	SUBCASE("stop using as base forgets a kept base and the main line it was")
	{
		ApplyLineChange(answers, ChangeOf(LineAnswerChange::Kind::StopUsingAsBase, "refs/remotes/origin/release"));

		expected.RemovedBases = {"refs/remotes/origin/develop", "refs/remotes/origin/release"};
		expected.KeptBases.clear();
		expected.BaseOrder = {"refs/heads/staging"};
		expected.MainLine.reset();
	}

	SUBCASE("stop using as base forgets an added base and leaves another main line")
	{
		ApplyLineChange(answers, ChangeOf(LineAnswerChange::Kind::StopUsingAsBase, "refs/heads/staging"));

		expected.RemovedBases = {"refs/remotes/origin/develop", "refs/heads/staging"};
		expected.AddedBases.clear();
		expected.BaseOrder = {"refs/remotes/origin/release"};
	}

	SUBCASE("stop using as base names a removed base once")
	{
		ApplyLineChange(answers, ChangeOf(LineAnswerChange::Kind::StopUsingAsBase, "refs/remotes/origin/develop"));
	}

	SUBCASE("use as main line answers the question and takes a removed base back")
	{
		ApplyLineChange(answers, ChangeOf(LineAnswerChange::Kind::UseAsMainLine, "refs/remotes/origin/develop"));

		expected.MainLine = "refs/remotes/origin/develop";
		expected.RemovedBases.clear();
	}

	SUBCASE("compare with names the target")
	{
		ApplyLineChange(answers, ChangeOf(LineAnswerChange::Kind::CompareWith, "refs/heads/topic", "refs/remotes/origin/release"));

		expected.Comparisons["refs/heads/topic"] = "refs/remotes/origin/release";
	}

	SUBCASE("compare with nothing keeps an empty answer")
	{
		ApplyLineChange(answers, ChangeOf(LineAnswerChange::Kind::CompareWithNothing, "refs/heads/topic"));

		expected.Comparisons["refs/heads/topic"] = std::nullopt;
	}

	SUBCASE("compare automatically forgets the answer")
	{
		ApplyLineChange(answers, ChangeOf(LineAnswerChange::Kind::CompareAutomatically, "refs/heads/topic"));

		expected.Comparisons.clear();
	}

	CHECK(answers == expected);
}

TEST_CASE("Git window: a recompute saves the change and recomputes on the last graph")
{
	GitFixture fixture;
	BuildMasterAndFeature(fixture);
	TempDirectory store;
	const LineStorePaths paths = StoreIn(fixture, store);
	const Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Core::Result<LineRead> read = ReadLines(repository, paths, kNow);
	REQUIRE(read.HasValue());
	const LineRead& last = read.GetValue();
	REQUIRE_FALSE(HasBase(last, "refs/heads/feature", Core::BaseReason::AddedByUser));

	const Core::Result<LineRead> recomputed = RecomputeLines(last, paths, ChangeOf(LineAnswerChange::Kind::UseAsBase, "refs/heads/feature"), kNow);
	REQUIRE(recomputed.HasValue());
	const LineRead& next = recomputed.GetValue();

	CHECK(next.Graph == last.Graph);
	CHECK(next.Generations == last.Generations);
	CHECK(next.Branches == last.Branches);
	CHECK(next.CommitsRead == 0);
	REQUIRE(next.Refs.size() == last.Refs.size());
	REQUIRE(next.Remotes.size() == 1);
	CHECK(HasBase(next, "refs/heads/feature", Core::BaseReason::AddedByUser));

	Core::LineAnswers expected = last.Answers;
	expected.AddedBases.push_back("refs/heads/feature");
	expected.BaseOrder.push_back("refs/heads/feature");
	CHECK(next.Answers == expected);
	CHECK(LoadLineAnswers(paths.Answers) == expected);
}

TEST_CASE("Git window: a change made on another repository's read is not saved in this one's store")
{
	GitFixture fixture;
	BuildMasterAndFeature(fixture);
	TempDirectory store;
	TempDirectory otherStore;
	const LineStorePaths paths = StoreIn(fixture, store);
	const LineStorePaths otherPaths{.Answers = otherStore.GetPath() / "answers.json", .Graph = otherStore.GetPath() / "graph.bin"};
	const Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Core::Result<LineRead> read = ReadLines(repository, paths, kNow);
	REQUIRE(read.HasValue());
	CHECK(read.GetValue().Store.Answers == paths.Answers);

	// The folder changed between the read and the change: the store now resolved is the other repository's.
	const Core::Result<LineRead> recomputed =
		RecomputeLines(read.GetValue(), otherPaths, ChangeOf(LineAnswerChange::Kind::UseAsBase, "refs/heads/feature"), kNow);

	REQUIRE_FALSE(recomputed.HasValue());
	CHECK(recomputed.GetError().Message == "The folder changed, so the change was not saved.");
	CHECK_FALSE(std::filesystem::exists(otherPaths.Answers));
}

TEST_CASE("Git window: a recompute refuses a file it cannot read")
{
	GitFixture fixture;
	BuildMasterAndFeature(fixture);
	TempDirectory store;
	const LineStorePaths paths = StoreIn(fixture, store);
	const Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Core::Result<LineRead> read = ReadLines(repository, paths, kNow);
	REQUIRE(read.HasValue());

	std::string text;

	SUBCASE("a file with a typo")
	{
		text = R"({"version": 1, "addedBases": [})";
	}

	SUBCASE("a file from a newer build")
	{
		text = R"({"version": 2})";
	}

	WriteText(paths.Answers, text);

	const Core::Result<LineRead> recomputed =
		RecomputeLines(read.GetValue(), paths, ChangeOf(LineAnswerChange::Kind::UseAsBase, "refs/heads/feature"), kNow);

	REQUIRE_FALSE(recomputed.HasValue());
	CHECK(recomputed.GetError().Message == "The line answers file cannot be read, so the change was not saved.");
	CHECK(ReadWhole(paths.Answers) == text);
}
