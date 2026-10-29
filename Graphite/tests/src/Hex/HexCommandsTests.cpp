#include <doctest/doctest.h>

#include "TempDirectory.h"

#include "Editor/EditorCommands.h"
#include "Hex/HexCommands.h"

#include "Core/Base/Result.h"

#include <cstdint>
#include <filesystem>
#include <string>

using Graphite::EditorAction;

namespace {

	constexpr std::uint64_t kEmpty = 0;
	constexpr std::uint64_t kFourBytes = 4;

	Graphite::HexSelection Between(std::uint64_t anchor, std::uint64_t active)
	{
		return Graphite::HexSelection{.Anchor = anchor, .Active = active};
	}
}

TEST_CASE("Copy and Select All run on a hex tab with bytes, and no other editing command does")
{
	CHECK(Graphite::CanRunOnHexTab(EditorAction::Copy, kFourBytes));
	CHECK(Graphite::CanRunOnHexTab(EditorAction::SelectAll, kFourBytes));

	for (const EditorAction action : {EditorAction::Cut, EditorAction::Paste, EditorAction::ToggleLineComment,
									  EditorAction::Undo, EditorAction::Redo})
	{
		CHECK_FALSE(Graphite::CanRunOnHexTab(action, kFourBytes));
	}

	CHECK_FALSE(Graphite::CanRunOnHexTab(EditorAction::Copy, kEmpty));
	CHECK_FALSE(Graphite::CanRunOnHexTab(EditorAction::SelectAll, kEmpty));
}

TEST_CASE("Select All in a hex tab selects from the first byte to the last")
{
	const Graphite::HexSelection all = Graphite::WholeFile(kFourBytes);

	CHECK(all.Anchor == 0);
	CHECK(all.Active == 3);
	CHECK(all.Count() == kFourBytes);
}

TEST_CASE("Copying a hex selection reads its bytes from the file as hex pairs, whichever way it was made")
{
	TempDirectory folder;
	const std::filesystem::path file = folder.MakeFile("program.exe", std::string("MZ\x90\0\x03", 5));

	const Core::Result<std::string> forwards = Graphite::CopySelectedBytes(file, Between(0, 3), 5);
	REQUIRE(forwards);
	CHECK(forwards.GetValue() == "4D 5A 90 00");

	const Core::Result<std::string> backwards = Graphite::CopySelectedBytes(file, Between(4, 2), 5);
	REQUIRE(backwards);
	CHECK(backwards.GetValue() == "90 00 03");
}

TEST_CASE("Copying a hex selection of exactly 1 MiB reads it all")
{
	TempDirectory folder;
	const std::filesystem::path file = folder.MakeFile("zeros.bin", std::string(Graphite::kMostCopiedBytes, '\0'));

	const Core::Result<std::string> copied =
		Graphite::CopySelectedBytes(file, Graphite::WholeFile(Graphite::kMostCopiedBytes), Graphite::kMostCopiedBytes);

	REQUIRE(copied);
	CHECK(copied.GetValue().size() == Graphite::kMostCopiedBytes * 3 - 1);
	CHECK(copied.GetValue().starts_with("00 00"));
}

TEST_CASE("Copying a hex selection past 1 MiB is refused before anything is read")
{
	// No file is there to read, so a refusal for any other reason than the size would be a read that was tried.
	TempDirectory folder;
	const std::filesystem::path missing = folder.GetPath() / "missing.bin";
	const std::uint64_t pastTheLimit = Graphite::kMostCopiedBytes + 1;

	const Core::Result<std::string> copied =
		Graphite::CopySelectedBytes(missing, Graphite::WholeFile(pastTheLimit), pastTheLimit);

	REQUIRE_FALSE(copied);
	CHECK(copied.GetError().Code == Core::ErrorCode::TooLarge);
	CHECK(copied.GetError().Message == "Select at most 1 MB to copy");
}

TEST_CASE("Copying a hex selection copies only the bytes a shrunk file still has, and nothing of an empty one")
{
	TempDirectory folder;
	const std::filesystem::path file = folder.MakeFile("short.bin", std::string("\0\x01", 2));

	const Core::Result<std::string> shrunk = Graphite::CopySelectedBytes(file, Between(0, 3), kFourBytes);
	REQUIRE(shrunk);
	CHECK(shrunk.GetValue() == "00 01");

	const Core::Result<std::string> empty = Graphite::CopySelectedBytes(folder.GetPath() / "missing.bin", Between(0, 0), kEmpty);
	REQUIRE(empty);
	CHECK(empty.GetValue().empty());
}

TEST_CASE("Copying a hex selection of a file that cannot be read gives the read's error")
{
	TempDirectory folder;

	const Core::Result<std::string> copied =
		Graphite::CopySelectedBytes(folder.GetPath() / "missing.bin", Between(0, 3), kFourBytes);

	REQUIRE_FALSE(copied);
	CHECK(copied.GetError().Code == Core::ErrorCode::NotFound);
}
