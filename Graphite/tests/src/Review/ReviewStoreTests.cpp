#include <doctest/doctest.h>

#include "Review/ReviewStore.h"

#include "TempDirectory.h"

#include <filesystem>
#include <fstream>
#include <string>

using Graphite::ReviewGroup;
using Graphite::ReviewStore;
using Graphite::ViewedState;

namespace {

	const std::string kBranch = "refs/heads/side";

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
}

TEST_CASE("Marks are saved once they stand unchanged for the delay and read back on the next open")
{
	TempDirectory folder;
	ReviewStore store;
	store.Open(folder.GetPath());

	store.GetMarks().Mark(kBranch, ReviewGroup::Commits, MarkedFile());
	Wait(store, 0.0f);
	Wait(store, ReviewStore::kSaveDelaySeconds - 0.5f);
	CHECK_FALSE(std::filesystem::exists(ReviewStore::FileFor(folder.GetPath())));

	Wait(store, 1.0f);
	CHECK(std::filesystem::exists(ReviewStore::FileFor(folder.GetPath())));

	ReviewStore reopened;
	reopened.Open(folder.GetPath());
	CHECK(reopened.GetMarks().StateOf(kBranch, ReviewGroup::Commits, MarkedFile()) == ViewedState::Viewed);
}

TEST_CASE("A change keeps restarting the save delay")
{
	TempDirectory folder;
	ReviewStore store;
	store.Open(folder.GetPath());

	store.GetMarks().Mark(kBranch, ReviewGroup::Commits, MarkedFile());
	Wait(store, 0.0f);
	Wait(store, ReviewStore::kSaveDelaySeconds - 0.5f);

	store.GetMarks().Unmark(kBranch, ReviewGroup::Commits, MarkedFile());
	Wait(store, 0.0f);
	Wait(store, 1.0f);

	CHECK_FALSE(std::filesystem::exists(ReviewStore::FileFor(folder.GetPath())));
}

TEST_CASE("Closing the folder writes what was not saved yet")
{
	TempDirectory folder;
	ReviewStore store;
	store.Open(folder.GetPath());
	store.GetMarks().Mark(kBranch, ReviewGroup::Commits, MarkedFile());

	store.Close();

	ReviewStore reopened;
	reopened.Open(folder.GetPath());
	CHECK(reopened.GetMarks().StateOf(kBranch, ReviewGroup::Commits, MarkedFile()) == ViewedState::Viewed);
	CHECK(store.GetMarks().GetAll().empty());
}

TEST_CASE("A folder without a reviews file opens with no marks and writes nothing")
{
	TempDirectory folder;
	ReviewStore store;
	store.Open(folder.GetPath());

	Wait(store, ReviewStore::kSaveDelaySeconds * 2.0f);
	store.Close();

	CHECK(store.GetMarks().GetAll().empty());
	CHECK_FALSE(std::filesystem::exists(ReviewStore::FileFor(folder.GetPath())));
}

TEST_CASE("A moved folder saves its marks to its new place")
{
	TempDirectory before;
	TempDirectory after;
	ReviewStore store;
	store.Open(before.GetPath());
	store.GetMarks().Mark(kBranch, ReviewGroup::Commits, MarkedFile());

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

	CHECK(store.GetMarks().GetAll().empty());
}
