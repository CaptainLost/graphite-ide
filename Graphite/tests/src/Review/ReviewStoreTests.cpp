#include <doctest/doctest.h>

#include "Review/ReviewStore.h"

#include "TempDirectory.h"

#include "Core/App/Redraw.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>

using Graphite::ReviewGroup;
using Graphite::ReviewStore;
using Graphite::ViewedState;

namespace {

	const std::string kBranch = "refs/heads/side";
	constexpr float kNoTime = 0.0f;
	constexpr float kHalfSecond = 0.5f;
	constexpr float kOneSecond = 1.0f;
	const float kJustBeforeTheDelay = ReviewStore::kSaveDelaySeconds - kHalfSecond;
	const float kWellPastTheDelay = ReviewStore::kSaveDelaySeconds * 2.0f;

	Core::FileDiff MarkedFile()
	{
		Core::FileDiff file;
		file.Change = Core::FileChange::Modified;
		file.OldPath = "a.cpp";
		file.NewPath = "a.cpp";
		file.OldId.Bytes[0] = 1;
		file.NewId.Bytes[0] = 2;
		return file;
	}

	void Wait(ReviewStore& store, float seconds)
	{
		store.Update(Core::Timestep{seconds});
	}

	void WriteReviewsFile(const std::filesystem::path& root, const std::string& text)
	{
		std::filesystem::create_directories(ReviewStore::FileFor(root).parent_path());
		std::ofstream{ReviewStore::FileFor(root), std::ios::binary} << text;
	}

	std::filesystem::path BackupFor(const std::filesystem::path& root)
	{
		std::filesystem::path backup = ReviewStore::FileFor(root);
		backup += ".bak";
		return backup;
	}

	std::string ReadWhole(const std::filesystem::path& path)
	{
		std::ifstream file{path, std::ios::binary};
		const std::istreambuf_iterator<char> endOfFile;
		return std::string{std::istreambuf_iterator<char>{file}, endOfFile};
	}

	bool ReopenedHoldsTheMark(const std::filesystem::path& root)
	{
		ReviewStore reopened;
		reopened.Open(root);
		return reopened.GetSessions().StateOf(kBranch, ReviewGroup::Commits, MarkedFile()) == ViewedState::Viewed;
	}
}

TEST_CASE("Marks are saved once they stand unchanged for the delay and read back on the next open")
{
	TempDirectory folder;
	ReviewStore store;
	store.Open(folder.GetPath());

	store.GetSessions().Start(kBranch, "refs/heads/main");
	store.GetSessions().Mark(kBranch, ReviewGroup::Commits, MarkedFile());
	Wait(store, kNoTime);
	Wait(store, kJustBeforeTheDelay);
	CHECK_FALSE(std::filesystem::exists(ReviewStore::FileFor(folder.GetPath())));

	Wait(store, kOneSecond);
	CHECK(std::filesystem::exists(ReviewStore::FileFor(folder.GetPath())));

	ReviewStore reopened;
	reopened.Open(folder.GetPath());
	CHECK(reopened.GetSessions().StateOf(kBranch, ReviewGroup::Commits, MarkedFile()) == ViewedState::Viewed);
}

TEST_CASE("A change keeps restarting the save delay")
{
	TempDirectory folder;
	ReviewStore store;
	store.Open(folder.GetPath());

	store.GetSessions().Start(kBranch, "refs/heads/main");
	store.GetSessions().Mark(kBranch, ReviewGroup::Commits, MarkedFile());
	Wait(store, kNoTime);
	Wait(store, kJustBeforeTheDelay);

	store.GetSessions().Unmark(kBranch, ReviewGroup::Commits, MarkedFile());
	Wait(store, kNoTime);
	Wait(store, kOneSecond);

	CHECK_FALSE(std::filesystem::exists(ReviewStore::FileFor(folder.GetPath())));
}

TEST_CASE("Closing the folder writes what was not saved yet")
{
	TempDirectory folder;
	ReviewStore store;
	store.Open(folder.GetPath());
	store.GetSessions().Start(kBranch, "refs/heads/main");
	store.GetSessions().Mark(kBranch, ReviewGroup::Commits, MarkedFile());

	store.Close();

	ReviewStore reopened;
	reopened.Open(folder.GetPath());
	CHECK(reopened.GetSessions().StateOf(kBranch, ReviewGroup::Commits, MarkedFile()) == ViewedState::Viewed);
	CHECK(store.GetSessions().GetAll().empty());
}

TEST_CASE("A folder without a reviews file opens with no marks and writes nothing")
{
	TempDirectory folder;
	ReviewStore store;
	store.Open(folder.GetPath());

	Wait(store, kWellPastTheDelay);
	store.Close();

	CHECK(store.GetSessions().GetAll().empty());
	CHECK_FALSE(std::filesystem::exists(ReviewStore::FileFor(folder.GetPath())));
}

TEST_CASE("A moved folder saves its marks to its new place")
{
	TempDirectory before;
	TempDirectory after;
	ReviewStore store;
	store.Open(before.GetPath());
	store.GetSessions().Start(kBranch, "refs/heads/main");
	store.GetSessions().Mark(kBranch, ReviewGroup::Commits, MarkedFile());

	store.MoveTo(after.GetPath());
	store.Flush();

	CHECK(std::filesystem::exists(ReviewStore::FileFor(after.GetPath())));
	CHECK_FALSE(std::filesystem::exists(ReviewStore::FileFor(before.GetPath())));
}

TEST_CASE("A malformed reviews file opens with no marks")
{
	TempDirectory folder;
	std::filesystem::create_directories(ReviewStore::FileFor(folder.GetPath()).parent_path());
	std::ofstream{ReviewStore::FileFor(folder.GetPath())} << "{ not json";

	ReviewStore store;
	store.Open(folder.GetPath());

	CHECK(store.GetSessions().GetAll().empty());
}

TEST_CASE("A malformed reviews file is copied aside before the first mark is written over it")
{
	TempDirectory folder;
	const std::string malformed = "{ not json";
	WriteReviewsFile(folder.GetPath(), malformed);
	ReviewStore store;
	store.Open(folder.GetPath());

	store.GetSessions().Start(kBranch, "refs/heads/main");
	store.GetSessions().Mark(kBranch, ReviewGroup::Commits, MarkedFile());
	store.Flush();

	REQUIRE(std::filesystem::exists(BackupFor(folder.GetPath())));
	CHECK(ReadWhole(BackupFor(folder.GetPath())) == malformed);
	CHECK(ReopenedHoldsTheMark(folder.GetPath()));
}

TEST_CASE("A reviews file of another version is copied aside before the first mark is written over it")
{
	TempDirectory folder;
	const std::string otherVersion = R"({"version": 3, "sessions": {"refs/heads/side": {"note": "kept"}}})";
	WriteReviewsFile(folder.GetPath(), otherVersion);
	ReviewStore store;
	store.Open(folder.GetPath());

	store.GetSessions().Start(kBranch, "refs/heads/main");
	store.GetSessions().Mark(kBranch, ReviewGroup::Commits, MarkedFile());
	store.Flush();

	REQUIRE(std::filesystem::exists(BackupFor(folder.GetPath())));
	CHECK(ReadWhole(BackupFor(folder.GetPath())) == otherVersion);
	CHECK(ReopenedHoldsTheMark(folder.GetPath()));
}

TEST_CASE("A readable reviews file is written over without a copy")
{
	TempDirectory folder;
	ReviewStore first;
	first.Open(folder.GetPath());
	first.GetSessions().Start(kBranch, "refs/heads/main");
	first.GetSessions().Mark(kBranch, ReviewGroup::Commits, MarkedFile());
	first.Close();

	ReviewStore second;
	second.Open(folder.GetPath());
	second.GetSessions().Unmark(kBranch, ReviewGroup::Commits, MarkedFile());
	second.Close();

	CHECK_FALSE(std::filesystem::exists(BackupFor(folder.GetPath())));
	CHECK_FALSE(ReopenedHoldsTheMark(folder.GetPath()));
}

TEST_CASE("A reviews file that cannot be read is left as it is after a mark and a flush")
{
	TempDirectory folder;
	const std::filesystem::path unreadable = ReviewStore::FileFor(folder.GetPath());
	std::filesystem::create_directories(unreadable);
	std::ofstream{unreadable / "inside.txt"} << "kept";
	ReviewStore store;
	store.Open(folder.GetPath());

	store.GetSessions().Start(kBranch, "refs/heads/main");
	store.GetSessions().Mark(kBranch, ReviewGroup::Commits, MarkedFile());
	store.Flush();
	store.Close();

	CHECK(std::filesystem::is_directory(unreadable));
	CHECK(ReadWhole(unreadable / "inside.txt") == "kept");
	CHECK_FALSE(std::filesystem::exists(BackupFor(folder.GetPath())));
}

TEST_CASE("A reviews file that could not be read is not written for the rest of the session")
{
	TempDirectory folder;
	const std::filesystem::path unreadable = ReviewStore::FileFor(folder.GetPath());
	std::filesystem::create_directories(unreadable);
	ReviewStore store;
	store.Open(folder.GetPath());
	std::filesystem::remove(unreadable);

	store.GetSessions().Start(kBranch, "refs/heads/main");
	store.GetSessions().Mark(kBranch, ReviewGroup::Commits, MarkedFile());
	Wait(store, kNoTime);
	Wait(store, kWellPastTheDelay);
	store.Flush();
	store.Close();

	CHECK_FALSE(std::filesystem::exists(unreadable));
}

TEST_CASE("Marks waiting for their save ask for the frame the save falls due on")
{
	TempDirectory folder;
	ReviewStore store;
	store.Open(folder.GetPath());
	store.GetSessions().Start(kBranch, "refs/heads/main");
	store.GetSessions().Mark(kBranch, ReviewGroup::Commits, MarkedFile());
	Wait(store, kNoTime);
	Core::Redraw::TakeDeadline();

	const float waited = kHalfSecond;
	Wait(store, waited);

	const std::optional<Core::Redraw::Clock::time_point> deadline = Core::Redraw::TakeDeadline();
	REQUIRE(deadline.has_value());
	CHECK(*deadline <= Core::Redraw::Clock::now() +
						   std::chrono::duration<double>(ReviewStore::kSaveDelaySeconds - waited));
}

TEST_CASE("A version 1 reviews file is copied aside and the folder opens with no sessions")
{
	TempDirectory folder;
	const std::string versionOne = R"({"version": 1, "branches": {"refs/heads/side": []}})";
	WriteReviewsFile(folder.GetPath(), versionOne);

	ReviewStore store;
	store.Open(folder.GetPath());
	CHECK(store.GetSessions().GetAll().empty());

	store.GetSessions().Start(kBranch, "refs/heads/main");
	store.Flush();

	CHECK(ReadWhole(BackupFor(folder.GetPath())) == versionOne);
}
