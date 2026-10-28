#include <doctest/doctest.h>

#include "Review/ReviewSessions.h"

#include "Core/FileSystem/FileSystem.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

using Graphite::MarkKey;
using Graphite::ReviewGroup;
using Graphite::ReviewSessions;
using Graphite::ReviewsText;
using Graphite::ViewedState;

namespace {

	const std::string kBranch = "refs/heads/feature/x";
	const std::string kBase = "refs/heads/main";

	Core::Oid IdNumbered(std::uint8_t number)
	{
		Core::Oid id;
		id.Bytes[0] = number;
		return id;
	}

	Core::FileDiff ModifiedFile(const std::filesystem::path& path, std::uint8_t oldNumber, std::uint8_t newNumber)
	{
		Core::FileDiff file;
		file.Change = Core::FileChange::Modified;
		file.OldPath = path;
		file.NewPath = path;
		file.OldId = IdNumbered(oldNumber);
		file.NewId = IdNumbered(newNumber);
		return file;
	}

	Core::Branch BranchNamed(const std::string& fullName)
	{
		Core::Branch branch;
		branch.FullName = fullName;
		return branch;
	}

	Graphite::ReviewRange RangeWithTipAndWorktree(std::uint8_t tip, const std::optional<std::filesystem::path>& worktree)
	{
		Graphite::ReviewRange range;
		range.Ends.Tip = IdNumbered(tip);
		range.Worktree = worktree;
		return range;
	}
}

TEST_CASE("A file is viewed while its change is the one that was marked")
{
	ReviewSessions sessions;
	sessions.Start(kBranch, kBase);
	const Core::FileDiff file = ModifiedFile("src/a.cpp", 1, 2);

	sessions.Mark(kBranch, ReviewGroup::Commits, file);

	CHECK(sessions.StateOf(kBranch, ReviewGroup::Commits, file) == ViewedState::Viewed);
}

TEST_CASE("A file whose change moved since it was marked reads as changed since viewed")
{
	ReviewSessions sessions;
	sessions.Start(kBranch, kBase);
	sessions.Mark(kBranch, ReviewGroup::Commits, ModifiedFile("src/a.cpp", 1, 2));

	CHECK(sessions.StateOf(kBranch, ReviewGroup::Commits, ModifiedFile("src/a.cpp", 1, 3)) ==
		  ViewedState::ChangedSinceViewed);
	CHECK(sessions.StateOf(kBranch, ReviewGroup::Commits, ModifiedFile("src/b.cpp", 1, 2)) == ViewedState::NotViewed);
}

TEST_CASE("Marking a file again replaces its earlier mark")
{
	ReviewSessions sessions;
	sessions.Start(kBranch, kBase);
	sessions.Mark(kBranch, ReviewGroup::Commits, ModifiedFile("src/a.cpp", 1, 2));
	sessions.Mark(kBranch, ReviewGroup::Commits, ModifiedFile("src/a.cpp", 1, 3));

	REQUIRE(sessions.Find(kBranch) != nullptr);
	CHECK(sessions.Find(kBranch)->Marks.size() == 1);
	CHECK(sessions.StateOf(kBranch, ReviewGroup::Commits, ModifiedFile("src/a.cpp", 1, 3)) == ViewedState::Viewed);
}

TEST_CASE("Unmarking a file leaves it not viewed and the session stays")
{
	ReviewSessions sessions;
	sessions.Start(kBranch, kBase);
	const Core::FileDiff file = ModifiedFile("src/a.cpp", 1, 2);
	sessions.Mark(kBranch, ReviewGroup::Commits, file);

	sessions.Unmark(kBranch, ReviewGroup::Commits, file);

	CHECK(sessions.StateOf(kBranch, ReviewGroup::Commits, file) == ViewedState::NotViewed);
	REQUIRE(sessions.Find(kBranch) != nullptr);
	CHECK(sessions.Find(kBranch)->Marks.empty());
}

TEST_CASE("The same file in both groups has two independent marks")
{
	ReviewSessions sessions;
	sessions.Start(kBranch, kBase);
	const Core::FileDiff committed = ModifiedFile("src/a.cpp", 1, 2);
	const Core::FileDiff uncommitted = ModifiedFile("src/a.cpp", 2, 9);

	sessions.Mark(kBranch, ReviewGroup::Commits, committed);

	CHECK(sessions.StateOf(kBranch, ReviewGroup::Uncommitted, uncommitted) == ViewedState::NotViewed);

	sessions.Mark(kBranch, ReviewGroup::Uncommitted, uncommitted);

	CHECK(sessions.StateOf(kBranch, ReviewGroup::Commits, committed) == ViewedState::Viewed);
	CHECK(sessions.StateOf(kBranch, ReviewGroup::Uncommitted, uncommitted) == ViewedState::Viewed);
}

TEST_CASE("A renamed file is marked under its new path and a deleted one under its old path")
{
	ReviewSessions sessions;
	sessions.Start(kBranch, kBase);

	Core::FileDiff renamed = ModifiedFile("src/old.cpp", 1, 2);
	renamed.Change = Core::FileChange::Renamed;
	renamed.NewPath = "src/new.cpp";

	Core::FileDiff deleted;
	deleted.Change = Core::FileChange::Deleted;
	deleted.OldPath = "src/gone.cpp";
	deleted.OldId = IdNumbered(5);

	sessions.Mark(kBranch, ReviewGroup::Commits, renamed);
	sessions.Mark(kBranch, ReviewGroup::Commits, deleted);

	REQUIRE(sessions.Find(kBranch) != nullptr);
	const Graphite::BranchMarks& held = sessions.Find(kBranch)->Marks;
	REQUIRE(held.size() == 2);
	CHECK(held.contains(MarkKey{.Group = ReviewGroup::Commits, .Path = "src/new.cpp"}));
	CHECK(held.contains(MarkKey{.Group = ReviewGroup::Commits, .Path = "src/gone.cpp"}));
}

TEST_CASE("Pruning drops the session of a branch only once two snapshots in a row no longer list it")
{
	ReviewSessions sessions;
	sessions.Start("refs/heads/kept", kBase);
	sessions.Start("refs/heads/gone", kBase);
	sessions.Mark("refs/heads/kept", ReviewGroup::Commits, ModifiedFile("a.cpp", 1, 2));
	sessions.Mark("refs/heads/gone", ReviewGroup::Commits, ModifiedFile("a.cpp", 1, 2));
	const std::vector<Core::Branch> withoutGone = {BranchNamed("refs/heads/kept"),
												   BranchNamed("refs/remotes/origin/kept")};

	sessions.Prune(withoutGone);

	CHECK(sessions.GetAll().contains("refs/heads/gone"));

	sessions.Prune(withoutGone);

	CHECK(sessions.GetAll().contains("refs/heads/kept"));
	CHECK_FALSE(sessions.GetAll().contains("refs/heads/gone"));
}

TEST_CASE("A branch listed again by the next snapshot starts its misses over")
{
	ReviewSessions sessions;
	sessions.Start(kBranch, kBase);
	const Core::FileDiff file = ModifiedFile("a.cpp", 1, 2);
	sessions.Mark(kBranch, ReviewGroup::Commits, file);
	const std::vector<Core::Branch> withoutBranch;
	const std::vector<Core::Branch> withBranch = {BranchNamed(kBranch)};

	sessions.Prune(withoutBranch);
	sessions.Prune(withBranch);
	sessions.Prune(withoutBranch);

	CHECK(sessions.StateOf(kBranch, ReviewGroup::Commits, file) == ViewedState::Viewed);
}

TEST_CASE("A rename moves a branch's marks to its new name")
{
	ReviewSessions sessions;
	sessions.Start("refs/heads/before", kBase);
	const Core::FileDiff file = ModifiedFile("a.cpp", 1, 2);
	sessions.Mark("refs/heads/before", ReviewGroup::Commits, file);

	sessions.Rename("refs/heads/before", "refs/heads/after");

	CHECK(sessions.StateOf("refs/heads/after", ReviewGroup::Commits, file) == ViewedState::Viewed);
	CHECK_FALSE(sessions.GetAll().contains("refs/heads/before"));
}

TEST_CASE("The version grows on every change and on nothing else")
{
	ReviewSessions sessions;
	const Core::FileDiff file = ModifiedFile("a.cpp", 1, 2);
	const std::uint64_t untouched = sessions.GetVersion();

	sessions.Unmark(kBranch, ReviewGroup::Commits, file);
	CHECK(sessions.GetVersion() == untouched);

	sessions.Start(kBranch, kBase);
	sessions.Mark(kBranch, ReviewGroup::Commits, file);
	CHECK(sessions.GetVersion() > untouched);
}

TEST_CASE("Marks survive an encode and a decode with slashes and non-ASCII characters in their names")
{
	ReviewSessions sessions;
	sessions.Start(kBranch, kBase);
	const std::filesystem::path nonAsciiPath = Core::FromUtf8("\xC5\xB9r\xC3\xB3"
															  "d\xC5\x82"
															  "a/plik.cpp");
	sessions.Mark(kBranch, ReviewGroup::Commits, ModifiedFile(nonAsciiPath, 1, 2));
	sessions.Mark(kBranch, ReviewGroup::Uncommitted, ModifiedFile("src/a.cpp", 3, 4));

	const Graphite::DecodedSessions decoded =
		Graphite::DecodeReviewSessionsText(Graphite::EncodeReviewSessions(sessions.GetAll()));

	CHECK(decoded.Sessions == sessions.GetAll());
}

TEST_CASE("A file that is not JSON or has another version decodes as no sessions")
{
	CHECK(Graphite::DecodeReviewSessionsText("not json").Sessions.empty());
	CHECK(Graphite::DecodeReviewSessionsText(R"({"version": 3, "sessions": {}})").Sessions.empty());
	CHECK(Graphite::DecodeReviewSessionsText("").Sessions.empty());
}

TEST_CASE("An entry with a bad id is left out and the rest are kept")
{
	const std::string goodId = IdNumbered(1).ToHex();
	const std::string json = std::string{R"({"version": 2, "sessions": {"refs/heads/x": {"base": "refs/heads/main", "marks": [)"} +
							 R"({"group": "commits", "path": "a.cpp", "old": ")" + goodId + R"(", "new": ")" + goodId + R"("},)" +
							 R"({"group": "commits", "path": "b.cpp", "old": "nothex", "new": ")" + goodId + R"("}]}}})";

	const Graphite::DecodedSessions decoded = Graphite::DecodeReviewSessionsText(json);

	REQUIRE(decoded.Sessions.contains("refs/heads/x"));
	const Graphite::BranchMarks& marks = decoded.Sessions.at("refs/heads/x").Marks;
	REQUIRE(marks.size() == 1);
	CHECK(marks.contains(MarkKey{.Group = ReviewGroup::Commits, .Path = "a.cpp"}));
}

TEST_CASE("Decoding tells a file this build reads from a malformed one and from another version")
{
	CHECK(Graphite::DecodeReviewSessionsText(R"({"version": 2, "sessions": {}})").Text == ReviewsText::Read);
	CHECK(Graphite::DecodeReviewSessionsText(R"({"version": 3, "sessions": {}})").Text == ReviewsText::OtherVersion);
	CHECK(Graphite::DecodeReviewSessionsText("not json").Text == ReviewsText::Malformed);
	CHECK(Graphite::DecodeReviewSessionsText("").Text == ReviewsText::Malformed);
	CHECK(Graphite::DecodeReviewSessionsText(R"({"version": "2", "sessions": {}})").Text == ReviewsText::Malformed);
	CHECK(Graphite::DecodeReviewSessionsText(R"({"version": 2})").Text == ReviewsText::Malformed);
}

TEST_CASE("Sessions are written in the version 2 shape")
{
	ReviewSessions sessions;
	sessions.Start("refs/heads/x", kBase);
	sessions.Mark("refs/heads/x", ReviewGroup::Uncommitted, ModifiedFile("b.cpp", 3, 4));
	const std::string oldId = IdNumbered(3).ToHex();
	const std::string newId = IdNumbered(4).ToHex();

	const std::string expected = std::string{"{\n"} +
								 "  \"sessions\": {\n" +
								 "    \"refs/heads/x\": {\n" +
								 "      \"base\": \"refs/heads/main\",\n" +
								 "      \"flat\": false,\n" +
								 "      \"hideViewed\": false,\n" +
								 "      \"hideWhitespace\": false,\n" +
								 "      \"includeUncommitted\": true,\n" +
								 "      \"marks\": [\n" +
								 "        {\n" +
								 "          \"group\": \"uncommitted\",\n" +
								 "          \"new\": \"" + newId + "\",\n" +
								 "          \"old\": \"" + oldId + "\",\n" +
								 "          \"path\": \"b.cpp\"\n" +
								 "        }\n" +
								 "      ],\n" +
								 "      \"sinceViewed\": true\n" +
								 "    }\n" +
								 "  },\n" +
								 "  \"version\": 2\n" +
								 "}";

	CHECK(Graphite::EncodeReviewSessions(sessions.GetAll()) == expected);
}

TEST_CASE("Decoding a file that marks one file twice in a group keeps the later mark")
{
	const std::string oldId = IdNumbered(1).ToHex();
	const std::string earlierNewId = IdNumbered(2).ToHex();
	const std::string laterNewId = IdNumbered(3).ToHex();
	const std::string json = std::string{R"({"version": 2, "sessions": {"refs/heads/x": {"base": "refs/heads/main", "marks": [)"} +
							 R"({"group": "commits", "path": "a.cpp", "old": ")" + oldId + R"(", "new": ")" + earlierNewId + R"("},)" +
							 R"({"group": "commits", "path": "a.cpp", "old": ")" + oldId + R"(", "new": ")" + laterNewId + R"("}]}}})";

	ReviewSessions sessions;
	sessions.Replace(Graphite::DecodeReviewSessionsText(json).Sessions);

	REQUIRE(sessions.Find("refs/heads/x") != nullptr);
	CHECK(sessions.Find("refs/heads/x")->Marks.size() == 1);
	CHECK(sessions.StateOf("refs/heads/x", ReviewGroup::Commits, ModifiedFile("a.cpp", 1, 3)) == ViewedState::Viewed);
}

TEST_CASE("Starting a session keeps one already under way with its base and marks")
{
	ReviewSessions sessions;
	sessions.Start(kBranch, kBase);
	sessions.Mark(kBranch, ReviewGroup::Commits, ModifiedFile("a.cpp", 1, 2));

	sessions.Start(kBranch, "refs/heads/other");

	REQUIRE(sessions.Find(kBranch) != nullptr);
	CHECK(sessions.Find(kBranch)->Base == kBase);
	CHECK(sessions.StateOf(kBranch, ReviewGroup::Commits, ModifiedFile("a.cpp", 1, 2)) == ViewedState::Viewed);
}

TEST_CASE("Marking a file of a branch without a session does nothing")
{
	ReviewSessions sessions;
	const std::uint64_t untouched = sessions.GetVersion();

	sessions.Mark(kBranch, ReviewGroup::Commits, ModifiedFile("a.cpp", 1, 2));

	CHECK(sessions.Find(kBranch) == nullptr);
	CHECK(sessions.GetVersion() == untouched);
}

TEST_CASE("Ending a session drops its marks")
{
	ReviewSessions sessions;
	sessions.Start(kBranch, kBase);
	sessions.Mark(kBranch, ReviewGroup::Commits, ModifiedFile("a.cpp", 1, 2));

	sessions.End(kBranch);

	CHECK(sessions.Find(kBranch) == nullptr);
	CHECK(sessions.StateOf(kBranch, ReviewGroup::Commits, ModifiedFile("a.cpp", 1, 2)) == ViewedState::NotViewed);
}

TEST_CASE("A session's settings change only when they differ")
{
	ReviewSessions sessions;
	sessions.Start(kBranch, kBase);
	const std::uint64_t before = sessions.GetVersion();

	const Graphite::SessionSettings started = sessions.Find(kBranch)->Settings;
	sessions.SetSettings(kBranch, started);
	CHECK(sessions.GetVersion() == before);

	Graphite::SessionSettings flat = started;
	flat.Shape = Graphite::ListShape::Flat;
	sessions.SetSettings(kBranch, flat);

	CHECK(sessions.GetVersion() > before);
	CHECK(sessions.Find(kBranch)->Settings.Shape == Graphite::ListShape::Flat);
}

TEST_CASE("The mark a file had is kept when it no longer matches")
{
	ReviewSessions sessions;
	sessions.Start(kBranch, kBase);
	sessions.Mark(kBranch, ReviewGroup::Commits, ModifiedFile("a.cpp", 1, 2));

	const std::optional<Graphite::ViewedMark> mark =
		sessions.MarkOf(kBranch, ReviewGroup::Commits, ModifiedFile("a.cpp", 1, 3));

	REQUIRE(mark.has_value());
	CHECK(mark->New == IdNumbered(2));
}

TEST_CASE("A rename moves a session and points sessions reviewing against the old name at the new one")
{
	ReviewSessions sessions;
	sessions.Start("refs/heads/before", kBase);
	sessions.Start("refs/heads/child", "refs/heads/before");

	sessions.Rename("refs/heads/before", "refs/heads/after");

	CHECK(sessions.Find("refs/heads/before") == nullptr);
	REQUIRE(sessions.Find("refs/heads/after") != nullptr);
	CHECK(sessions.Find("refs/heads/after")->Base == kBase);
	CHECK(sessions.Find("refs/heads/child")->Base == "refs/heads/after");
}

TEST_CASE("A session whose base is gone is kept with its marks")
{
	ReviewSessions sessions;
	sessions.Start(kBranch, kBase);
	sessions.Mark(kBranch, ReviewGroup::Commits, ModifiedFile("a.cpp", 1, 2));
	const std::vector<Core::Branch> withoutBase = {BranchNamed(kBranch)};

	sessions.Prune(withoutBase);
	sessions.Prune(withoutBase);

	REQUIRE(sessions.Find(kBranch) != nullptr);
	CHECK(sessions.StateOf(kBranch, ReviewGroup::Commits, ModifiedFile("a.cpp", 1, 2)) == ViewedState::Viewed);
}

TEST_CASE("Sessions with their settings survive an encode and a decode")
{
	ReviewSessions sessions;
	sessions.Start(kBranch, "refs/remotes/origin/main");
	Graphite::SessionSettings settings;
	settings.IncludeUncommitted = true;
	settings.Shape = Graphite::ListShape::Flat;
	settings.HideViewed = true;
	settings.Whitespace = Core::WhitespaceInDiff::Ignored;
	settings.ChangedFiles = Graphite::ChangedFileView::AllChanges;
	sessions.SetSettings(kBranch, settings);
	sessions.Mark(kBranch, ReviewGroup::Uncommitted, ModifiedFile("src/a.cpp", 3, 4));

	const Graphite::DecodedSessions decoded =
		Graphite::DecodeReviewSessionsText(Graphite::EncodeReviewSessions(sessions.GetAll()));

	CHECK(decoded.Text == Graphite::ReviewsText::Read);
	CHECK(decoded.Sessions == sessions.GetAll());
}

TEST_CASE("A version 1 reviews file reads as another version")
{
	const Graphite::DecodedSessions decoded =
		Graphite::DecodeReviewSessionsText(R"({"version": 1, "branches": {"refs/heads/x": []}})");

	CHECK(decoded.Text == Graphite::ReviewsText::OtherVersion);
	CHECK(decoded.Sessions.empty());
}

TEST_CASE("A session without a base is left out and the others are kept")
{
	const Graphite::DecodedSessions decoded = Graphite::DecodeReviewSessionsText(
		R"({"version": 2, "sessions": {"refs/heads/a": {"marks": []}, "refs/heads/b": {"base": "refs/heads/main"}}})");

	CHECK(decoded.Text == Graphite::ReviewsText::Read);
	CHECK_FALSE(decoded.Sessions.contains("refs/heads/a"));
	REQUIRE(decoded.Sessions.contains("refs/heads/b"));
	CHECK(decoded.Sessions.at("refs/heads/b").Settings == Graphite::SessionSettings{});
}

TEST_CASE("A committed file changed since it was viewed is diffed from the viewed version to the tip")
{
	ReviewSessions sessions;
	sessions.Start(kBranch, kBase);
	sessions.Mark(kBranch, ReviewGroup::Commits, ModifiedFile("src/a.cpp", 1, 2));
	const Graphite::ReviewRange range = RangeWithTipAndWorktree(9, std::nullopt);

	const std::optional<Graphite::SinceViewedRequest> request = Graphite::SinceViewedRequestFor(
		sessions, kBranch, ReviewGroup::Commits, ModifiedFile("src/a.cpp", 1, 3), range, Core::WhitespaceInDiff::Ignored);

	REQUIRE(request.has_value());
	CHECK(request->ViewedBlob == IdNumbered(2));
	CHECK(request->CurrentBlob == IdNumbered(3));
	CHECK(request->Path == std::filesystem::path{"src/a.cpp"});
	CHECK(request->Whitespace == Core::WhitespaceInDiff::Ignored);
	CHECK(request->Tip == std::optional<Core::Oid>{IdNumbered(9)});
	CHECK_FALSE(request->Worktree.has_value());
}

TEST_CASE("An uncommitted file changed since it was viewed is diffed against its worktree and not without one")
{
	ReviewSessions sessions;
	sessions.Start(kBranch, kBase);
	sessions.Mark(kBranch, ReviewGroup::Uncommitted, ModifiedFile("a.cpp", 1, 2));
	const Core::FileDiff now = ModifiedFile("a.cpp", 1, 3);
	const std::filesystem::path worktree = "D:/work/side";

	const std::optional<Graphite::SinceViewedRequest> request = Graphite::SinceViewedRequestFor(
		sessions, kBranch, ReviewGroup::Uncommitted, now, RangeWithTipAndWorktree(9, worktree),
		Core::WhitespaceInDiff::Shown);

	REQUIRE(request.has_value());
	CHECK(request->Worktree == std::optional<std::filesystem::path>{worktree});
	CHECK_FALSE(request->Tip.has_value());

	CHECK_FALSE(Graphite::SinceViewedRequestFor(sessions, kBranch, ReviewGroup::Uncommitted, now,
												RangeWithTipAndWorktree(9, std::nullopt),
												Core::WhitespaceInDiff::Shown)
					.has_value());
}

TEST_CASE("A file viewed as it is, changed only in its base, or deleted has no diff since it was viewed")
{
	ReviewSessions sessions;
	sessions.Start(kBranch, kBase);
	sessions.Mark(kBranch, ReviewGroup::Commits, ModifiedFile("a.cpp", 1, 2));
	const Graphite::ReviewRange range = RangeWithTipAndWorktree(9, std::nullopt);
	const Core::WhitespaceInDiff shown = Core::WhitespaceInDiff::Shown;

	CHECK_FALSE(Graphite::SinceViewedRequestFor(sessions, kBranch, ReviewGroup::Commits, ModifiedFile("a.cpp", 1, 2),
												range, shown)
					.has_value());
	CHECK_FALSE(Graphite::SinceViewedRequestFor(sessions, kBranch, ReviewGroup::Commits, ModifiedFile("a.cpp", 5, 2),
												range, shown)
					.has_value());

	Core::FileDiff deleted = ModifiedFile("a.cpp", 1, 0);
	deleted.Change = Core::FileChange::Deleted;
	CHECK_FALSE(Graphite::SinceViewedRequestFor(sessions, kBranch, ReviewGroup::Commits, deleted, range, shown).has_value());
}

TEST_CASE("A draft holds settings but is not kept")
{
	ReviewSessions sessions;
	sessions.OpenDraft(kBranch, kBase);

	REQUIRE(sessions.Find(kBranch) != nullptr);
	CHECK(sessions.Find(kBranch)->Base == kBase);
	CHECK(sessions.Find(kBranch)->Draft);
	CHECK_FALSE(sessions.IsKept(kBranch));

	Graphite::SessionSettings flat;
	flat.Shape = Graphite::ListShape::Flat;
	sessions.SetSettings(kBranch, flat);

	CHECK(sessions.Find(kBranch)->Settings.Shape == Graphite::ListShape::Flat);
	CHECK_FALSE(sessions.IsKept(kBranch));
	CHECK_FALSE(sessions.IsKept("refs/heads/unknown"));
}

TEST_CASE("A draft opens with uncommitted work included")
{
	ReviewSessions sessions;
	sessions.OpenDraft(kBranch, kBase);

	CHECK(sessions.Find(kBranch)->Settings.IncludeUncommitted);
}

TEST_CASE("A review started on a branch with no draft includes uncommitted work, as a draft does")
{
	ReviewSessions sessions;
	sessions.Start(kBranch, kBase);

	REQUIRE(sessions.Find(kBranch) != nullptr);
	CHECK(sessions.Find(kBranch)->Settings.IncludeUncommitted);
	CHECK(sessions.IsKept(kBranch));
}

TEST_CASE("The first mark keeps a draft")
{
	ReviewSessions sessions;
	sessions.OpenDraft(kBranch, kBase);
	const Core::FileDiff file = ModifiedFile("a.cpp", 1, 2);

	sessions.Mark(kBranch, ReviewGroup::Commits, file);

	CHECK(sessions.IsKept(kBranch));
	CHECK_FALSE(sessions.Find(kBranch)->Draft);
	CHECK(sessions.StateOf(kBranch, ReviewGroup::Commits, file) == ViewedState::Viewed);
}

TEST_CASE("Starting a review over a draft keeps its settings and takes the new base")
{
	ReviewSessions sessions;
	sessions.OpenDraft(kBranch, kBase);
	Graphite::SessionSettings flat;
	flat.Shape = Graphite::ListShape::Flat;
	sessions.SetSettings(kBranch, flat);

	sessions.Start(kBranch, "refs/heads/other");

	CHECK(sessions.IsKept(kBranch));
	CHECK(sessions.Find(kBranch)->Base == "refs/heads/other");
	CHECK(sessions.Find(kBranch)->Settings == flat);
}

TEST_CASE("Opening a draft over a kept session changes nothing")
{
	ReviewSessions sessions;
	sessions.Start(kBranch, kBase);
	const std::uint64_t before = sessions.GetVersion();

	sessions.OpenDraft(kBranch, "refs/heads/other");

	CHECK(sessions.GetVersion() == before);
	CHECK(sessions.Find(kBranch)->Base == kBase);
	CHECK(sessions.IsKept(kBranch));

	sessions.OpenDraft("refs/heads/second", kBase);
	const std::uint64_t withDraft = sessions.GetVersion();
	sessions.OpenDraft("refs/heads/second", "refs/heads/other");

	CHECK(sessions.GetVersion() == withDraft);
	CHECK(sessions.Find("refs/heads/second")->Base == kBase);
}

TEST_CASE("Keeping turns a draft into a session and leaves anything else alone")
{
	ReviewSessions sessions;
	sessions.OpenDraft(kBranch, kBase);

	sessions.Keep(kBranch);
	CHECK(sessions.IsKept(kBranch));

	const std::uint64_t before = sessions.GetVersion();
	sessions.Keep(kBranch);
	sessions.Keep("refs/heads/unknown");

	CHECK(sessions.GetVersion() == before);
	CHECK(sessions.Find("refs/heads/unknown") == nullptr);
}

TEST_CASE("Pruning and renaming treat drafts like sessions")
{
	ReviewSessions sessions;
	sessions.OpenDraft("refs/heads/before", kBase);

	sessions.Rename("refs/heads/before", "refs/heads/after");

	REQUIRE(sessions.Find("refs/heads/after") != nullptr);
	CHECK(sessions.Find("refs/heads/after")->Draft);
	CHECK(sessions.Find("refs/heads/before") == nullptr);

	const std::vector<Core::Branch> none;
	sessions.Prune(none);
	CHECK(sessions.Find("refs/heads/after") != nullptr);
	sessions.Prune(none);
	CHECK(sessions.Find("refs/heads/after") == nullptr);
}

TEST_CASE("Drafts are left out of reviews.json")
{
	ReviewSessions sessions;
	sessions.OpenDraft("refs/heads/looked-at", kBase);
	sessions.Start(kBranch, kBase);

	const Graphite::DecodedSessions decoded =
		Graphite::DecodeReviewSessionsText(Graphite::EncodeReviewSessions(sessions.GetAll()));

	REQUIRE(decoded.Sessions.size() == 1);
	CHECK(decoded.Sessions.contains(kBranch));
}
