#include <doctest/doctest.h>

#include "Review/ViewedMarks.h"

#include "Core/FileSystem/FileSystem.h"

#include <filesystem>
#include <string>
#include <vector>

using Graphite::MarkKey;
using Graphite::MarksText;
using Graphite::ReviewGroup;
using Graphite::ViewedMarks;
using Graphite::ViewedState;

namespace {

	const std::string kBranch = "refs/heads/feature/x";

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
}

TEST_CASE("A file is viewed while its change is the one that was marked")
{
	ViewedMarks marks;
	const Core::FileDiff file = ModifiedFile("src/a.cpp", 1, 2);

	marks.Mark(kBranch, ReviewGroup::Commits, file);

	CHECK(marks.StateOf(kBranch, ReviewGroup::Commits, file) == ViewedState::Viewed);
}

TEST_CASE("A file whose change moved since it was marked reads as changed since viewed")
{
	ViewedMarks marks;
	marks.Mark(kBranch, ReviewGroup::Commits, ModifiedFile("src/a.cpp", 1, 2));

	CHECK(marks.StateOf(kBranch, ReviewGroup::Commits, ModifiedFile("src/a.cpp", 1, 3)) ==
		  ViewedState::ChangedSinceViewed);
	CHECK(marks.StateOf(kBranch, ReviewGroup::Commits, ModifiedFile("src/b.cpp", 1, 2)) == ViewedState::NotViewed);
}

TEST_CASE("Marking a file again replaces its earlier mark")
{
	ViewedMarks marks;
	marks.Mark(kBranch, ReviewGroup::Commits, ModifiedFile("src/a.cpp", 1, 2));
	marks.Mark(kBranch, ReviewGroup::Commits, ModifiedFile("src/a.cpp", 1, 3));

	CHECK(marks.GetAll().at(kBranch).size() == 1);
	CHECK(marks.StateOf(kBranch, ReviewGroup::Commits, ModifiedFile("src/a.cpp", 1, 3)) == ViewedState::Viewed);
}

TEST_CASE("Unmarking a file leaves it not viewed and an emptied branch is dropped")
{
	ViewedMarks marks;
	const Core::FileDiff file = ModifiedFile("src/a.cpp", 1, 2);
	marks.Mark(kBranch, ReviewGroup::Commits, file);

	marks.Unmark(kBranch, ReviewGroup::Commits, file);

	CHECK(marks.StateOf(kBranch, ReviewGroup::Commits, file) == ViewedState::NotViewed);
	CHECK(marks.GetAll().empty());
}

TEST_CASE("The same file in both groups has two independent marks")
{
	ViewedMarks marks;
	const Core::FileDiff committed = ModifiedFile("src/a.cpp", 1, 2);
	const Core::FileDiff uncommitted = ModifiedFile("src/a.cpp", 2, 9);

	marks.Mark(kBranch, ReviewGroup::Commits, committed);

	CHECK(marks.StateOf(kBranch, ReviewGroup::Uncommitted, uncommitted) == ViewedState::NotViewed);

	marks.Mark(kBranch, ReviewGroup::Uncommitted, uncommitted);

	CHECK(marks.StateOf(kBranch, ReviewGroup::Commits, committed) == ViewedState::Viewed);
	CHECK(marks.StateOf(kBranch, ReviewGroup::Uncommitted, uncommitted) == ViewedState::Viewed);
}

TEST_CASE("A renamed file is marked under its new path and a deleted one under its old path")
{
	ViewedMarks marks;

	Core::FileDiff renamed = ModifiedFile("src/old.cpp", 1, 2);
	renamed.Change = Core::FileChange::Renamed;
	renamed.NewPath = "src/new.cpp";

	Core::FileDiff deleted;
	deleted.Change = Core::FileChange::Deleted;
	deleted.OldPath = "src/gone.cpp";
	deleted.OldId = IdNumbered(5);

	marks.Mark(kBranch, ReviewGroup::Commits, renamed);
	marks.Mark(kBranch, ReviewGroup::Commits, deleted);

	const ViewedMarks::BranchMarks& held = marks.GetAll().at(kBranch);
	REQUIRE(held.size() == 2);
	CHECK(held.contains(MarkKey{.Group = ReviewGroup::Commits, .Path = "src/new.cpp"}));
	CHECK(held.contains(MarkKey{.Group = ReviewGroup::Commits, .Path = "src/gone.cpp"}));
}

TEST_CASE("Pruning drops the marks of a branch only once two snapshots in a row no longer list it")
{
	ViewedMarks marks;
	marks.Mark("refs/heads/kept", ReviewGroup::Commits, ModifiedFile("a.cpp", 1, 2));
	marks.Mark("refs/heads/gone", ReviewGroup::Commits, ModifiedFile("a.cpp", 1, 2));
	const std::vector<Core::Branch> withoutGone = {BranchNamed("refs/heads/kept"),
												   BranchNamed("refs/remotes/origin/kept")};

	marks.Prune(withoutGone);

	CHECK(marks.GetAll().contains("refs/heads/gone"));

	marks.Prune(withoutGone);

	CHECK(marks.GetAll().contains("refs/heads/kept"));
	CHECK_FALSE(marks.GetAll().contains("refs/heads/gone"));
}

TEST_CASE("A branch listed again by the next snapshot starts its misses over")
{
	ViewedMarks marks;
	const Core::FileDiff file = ModifiedFile("a.cpp", 1, 2);
	marks.Mark(kBranch, ReviewGroup::Commits, file);
	const std::vector<Core::Branch> withoutBranch;
	const std::vector<Core::Branch> withBranch = {BranchNamed(kBranch)};

	marks.Prune(withoutBranch);
	marks.Prune(withBranch);
	marks.Prune(withoutBranch);

	CHECK(marks.StateOf(kBranch, ReviewGroup::Commits, file) == ViewedState::Viewed);
}

TEST_CASE("A rename moves a branch's marks to its new name")
{
	ViewedMarks marks;
	const Core::FileDiff file = ModifiedFile("a.cpp", 1, 2);
	marks.Mark("refs/heads/before", ReviewGroup::Commits, file);

	marks.Rename("refs/heads/before", "refs/heads/after");

	CHECK(marks.StateOf("refs/heads/after", ReviewGroup::Commits, file) == ViewedState::Viewed);
	CHECK_FALSE(marks.GetAll().contains("refs/heads/before"));
}

TEST_CASE("The version grows on every change and on nothing else")
{
	ViewedMarks marks;
	const Core::FileDiff file = ModifiedFile("a.cpp", 1, 2);
	const std::uint64_t untouched = marks.GetVersion();

	marks.Unmark(kBranch, ReviewGroup::Commits, file);
	CHECK(marks.GetVersion() == untouched);

	marks.Mark(kBranch, ReviewGroup::Commits, file);
	CHECK(marks.GetVersion() > untouched);
}

TEST_CASE("Marks survive an encode and a decode with slashes and non-ASCII characters in their names")
{
	ViewedMarks marks;
	const std::filesystem::path nonAsciiPath = Core::FromUtf8("\xC5\xB9r\xC3\xB3" "d\xC5\x82" "a/plik.cpp");
	marks.Mark(kBranch, ReviewGroup::Commits, ModifiedFile(nonAsciiPath, 1, 2));
	marks.Mark(kBranch, ReviewGroup::Uncommitted, ModifiedFile("src/a.cpp", 3, 4));

	const ViewedMarks::Marks decoded = Graphite::DecodeViewedMarks(Graphite::EncodeViewedMarks(marks.GetAll()));

	CHECK(decoded == marks.GetAll());
}

TEST_CASE("A file that is not JSON or has another version decodes as no marks")
{
	CHECK(Graphite::DecodeViewedMarks("not json").empty());
	CHECK(Graphite::DecodeViewedMarks(R"({"version": 2, "branches": {}})").empty());
	CHECK(Graphite::DecodeViewedMarks("").empty());
}

TEST_CASE("An entry with a bad id is left out and the rest are kept")
{
	const std::string goodId = IdNumbered(1).ToHex();
	const std::string json = std::string{R"({"version": 1, "branches": {"refs/heads/x": [)"} +
							 R"({"group": "commits", "path": "a.cpp", "old": ")" + goodId + R"(", "new": ")" + goodId + R"("},)" +
							 R"({"group": "commits", "path": "b.cpp", "old": "nothex", "new": ")" + goodId + R"("}]}})";

	const ViewedMarks::Marks decoded = Graphite::DecodeViewedMarks(json);

	REQUIRE(decoded.contains("refs/heads/x"));
	REQUIRE(decoded.at("refs/heads/x").size() == 1);
	CHECK(decoded.at("refs/heads/x").contains(MarkKey{.Group = ReviewGroup::Commits, .Path = "a.cpp"}));
}

TEST_CASE("Decoding tells a file this build reads from a malformed one and from another version")
{
	CHECK(Graphite::DecodeViewedMarksText(R"({"version": 1, "branches": {}})").Text == MarksText::Read);
	CHECK(Graphite::DecodeViewedMarksText(R"({"version": 2, "branches": {}})").Text == MarksText::OtherVersion);
	CHECK(Graphite::DecodeViewedMarksText("not json").Text == MarksText::Malformed);
	CHECK(Graphite::DecodeViewedMarksText("").Text == MarksText::Malformed);
	CHECK(Graphite::DecodeViewedMarksText(R"({"version": "1", "branches": {}})").Text == MarksText::Malformed);
	CHECK(Graphite::DecodeViewedMarksText(R"({"version": 1})").Text == MarksText::Malformed);
}

TEST_CASE("Marks are written in the shape the reviews file has always had")
{
	ViewedMarks marks;
	marks.Mark("refs/heads/x", ReviewGroup::Uncommitted, ModifiedFile("b.cpp", 3, 4));
	const std::string oldId = IdNumbered(3).ToHex();
	const std::string newId = IdNumbered(4).ToHex();

	const std::string expected = std::string{"{\n"} +
								 "  \"branches\": {\n" +
								 "    \"refs/heads/x\": [\n" +
								 "      {\n" +
								 "        \"group\": \"uncommitted\",\n" +
								 "        \"new\": \"" + newId + "\",\n" +
								 "        \"old\": \"" + oldId + "\",\n" +
								 "        \"path\": \"b.cpp\"\n" +
								 "      }\n" +
								 "    ]\n" +
								 "  },\n" +
								 "  \"version\": 1\n" +
								 "}";

	CHECK(Graphite::EncodeViewedMarks(marks.GetAll()) == expected);
}

TEST_CASE("Decoding a file that marks one file twice in a group keeps the later mark")
{
	const std::string oldId = IdNumbered(1).ToHex();
	const std::string earlierNewId = IdNumbered(2).ToHex();
	const std::string laterNewId = IdNumbered(3).ToHex();
	const std::string json = std::string{R"({"version": 1, "branches": {"refs/heads/x": [)"} +
							 R"({"group": "commits", "path": "a.cpp", "old": ")" + oldId + R"(", "new": ")" + earlierNewId + R"("},)" +
							 R"({"group": "commits", "path": "a.cpp", "old": ")" + oldId + R"(", "new": ")" + laterNewId + R"("}]}})";

	ViewedMarks marks;
	marks.Replace(Graphite::DecodeViewedMarks(json));

	CHECK(marks.GetAll().at("refs/heads/x").size() == 1);
	CHECK(marks.StateOf("refs/heads/x", ReviewGroup::Commits, ModifiedFile("a.cpp", 1, 3)) == ViewedState::Viewed);
}
