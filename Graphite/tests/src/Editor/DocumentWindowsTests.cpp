#include <doctest/doctest.h>

#include "Editor/DocumentWindows.h"
#include "UI/FileIcons.h"

#include "TempDirectory.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Text/DocumentSet.h"

#include <cstddef>
#include <filesystem>
#include <string>

TEST_CASE("A save failure whose error names the file is shown as it is")
{
	const std::filesystem::path path("D:/Repos/IDE/src/a.cpp");

	CHECK(Graphite::SaveFailureMessage(path, "Cannot write 'D:/Repos/IDE/src/a.cpp': access denied") ==
		  "Cannot write 'D:/Repos/IDE/src/a.cpp': access denied");
}

TEST_CASE("A save failure whose error does not name the file is prefixed with its path")
{
	const std::filesystem::path path("D:/Repos/IDE/src/a.cpp");

	CHECK(Graphite::SaveFailureMessage(path, "cannot write 'D:/Temp/x1.tmp': disk full") ==
		  "D:/Repos/IDE/src/a.cpp: cannot write 'D:/Temp/x1.tmp': disk full");
	CHECK(Graphite::SaveFailureMessage(path, "access denied") == "D:/Repos/IDE/src/a.cpp: access denied");
}

TEST_CASE("A document's window name starts with its file kind's tab glyph, not its badge, and is identified by its path")
{
	TempDirectory folder;
	const std::filesystem::path notes = folder.MakeFile("README.md", "# Notes");

	Core::DocumentSet documents;
	REQUIRE(documents.Open(notes, Core::TabKind::Permanent) == Core::OpenOutcome::Opened);

	constexpr std::size_t kOnlyDocument = 0;
	const std::string name = Graphite::DocumentWindowName(documents, kOnlyDocument);

	CHECK(name.starts_with(Graphite::GlyphOf(Graphite::FileKind::Markdown)));
	CHECK_FALSE(name.starts_with(Graphite::BadgeOf(Graphite::FileKind::Markdown)));
	CHECK(name.ends_with("###" + Core::ToUtf8(documents.At(kOnlyDocument).Path)));
}
