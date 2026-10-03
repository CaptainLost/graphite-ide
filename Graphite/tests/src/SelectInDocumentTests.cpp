#include <doctest/doctest.h>

#include "Editor/Navigation/SelectInDocument.h"

#include "Core/Text/DocumentSet.h"

#include "TempDirectory.h"

namespace {

	using Graphite::Utf16Column;

	// Opens the way DocumentWindows would for a file inside the size limit.
	Graphite::OpenDocument OpenPermanently(Core::DocumentSet& documents)
	{
		return [&documents](const std::filesystem::path& path) { documents.Open(path, true); };
	}
}

TEST_CASE("A byte range on a line is selected and the editor is told to scroll to it")
{
	TempDirectory directory;
	const auto path = directory.MakeFile("a.cpp", "first\nsecond line\n");

	Core::DocumentSet documents;

	REQUIRE(Graphite::SelectInDocument(documents, path, 1, 3, 6, OpenPermanently(documents)));

	const Core::OpenItem& item = documents.At(0);
	CHECK(item.Caret.Anchor == 6 + 3);
	CHECK(item.Caret.Offset == 6 + 6);
	CHECK(item.PendingScroll);
	CHECK(item.RowAimAt == Core::NoRowAim);
}

TEST_CASE("A range past the end of a shrunk file is clamped rather than read past it")
{
	TempDirectory directory;
	const auto path = directory.MakeFile("a.cpp", "abc\n");

	Core::DocumentSet documents;

	REQUIRE(Graphite::SelectInDocument(documents, path, 0, 100, 200, OpenPermanently(documents)));

	CHECK(documents.At(0).Caret.Offset <= documents.At(0).Document->GetBuffer().Materialize().size());
}

TEST_CASE("Nothing is selected when the opener produced no document")
{
	TempDirectory directory;
	const auto path = directory.GetPath() / "missing.cpp";

	Core::DocumentSet documents;
	bool opened = false;

	CHECK_FALSE(Graphite::SelectInDocument(
		documents, path, 0, 0, 1, [&opened](const std::filesystem::path&) { opened = true; }));
	CHECK(opened);
}

TEST_CASE("UTF-16 columns are converted to bytes against the line's own text")
{
	TempDirectory directory;
	// "é" is two bytes but one UTF-16 unit, so columns 3..6 name bytes 4..7.
	const auto path = directory.MakeFile("a.txt", "x\n\xc3\xa9 abcd\n");

	Core::DocumentSet documents;

	REQUIRE(Graphite::SelectInDocument(documents, path, 1, Utf16Column(2), 1, Utf16Column(5),
									   OpenPermanently(documents)));

	const Core::OpenItem& item = documents.At(0);
	CHECK(item.Caret.Anchor == 2 + 3);
	CHECK(item.Caret.Offset == 2 + 6);
}

TEST_CASE("A UTF-16 range that leaves its line selects only the point it starts at")
{
	TempDirectory directory;
	const auto path = directory.MakeFile("a.txt", "abcdef\nghi\n");

	Core::DocumentSet documents;

	REQUIRE(Graphite::SelectInDocument(documents, path, 0, Utf16Column(2), 1, Utf16Column(1),
									   OpenPermanently(documents)));

	CHECK(documents.At(0).Caret.Anchor == 2);
	CHECK(documents.At(0).Caret.Offset == 2);
}

TEST_CASE("A span is one line only when it ends on its own line past where it starts")
{
	CHECK(Graphite::SpansOneLine(3, Utf16Column(2), 3, Utf16Column(5)));
	CHECK_FALSE(Graphite::SpansOneLine(3, Utf16Column(2), 3, Utf16Column(2)));
	CHECK_FALSE(Graphite::SpansOneLine(3, Utf16Column(2), 3, Utf16Column(1)));
	CHECK_FALSE(Graphite::SpansOneLine(3, Utf16Column(2), 4, Utf16Column(9)));
}
