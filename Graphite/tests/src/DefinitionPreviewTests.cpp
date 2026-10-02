#include <doctest/doctest.h>

#include "Editor/DefinitionPreview.h"

#include "Core/Text/DocumentSet.h"
#include "Core/Text/Editing.h"
#include "Core/Text/TextDocument.h"

#include "TempDirectory.h"

#include <fstream>

using Graphite::DefinitionPreview;
using Graphite::LoadDefinitionPreview;

namespace {
	void Write(const std::filesystem::path& path, std::string_view bytes)
	{
		std::ofstream out(path, std::ios::binary);
		out << bytes;
	}
}

TEST_CASE("A preview from disk is cut to the definition")
{
	TempDirectory dir;
	const auto file = dir.GetPath() / "Layer.h";
	Write(file, "#pragma once\nint Sum(int a, int b)\n{\n    return a + b;\n}\nint After;\n");

	Core::DocumentSet documents;
	const DefinitionPreview preview = LoadDefinitionPreview(documents, file, 1);

	CHECK(preview.FirstLine == 1);
	CHECK(preview.Lines ==
		  std::vector<std::string>{"int Sum(int a, int b)", "{", "    return a + b;", "}"});
}

TEST_CASE("A preview keeps the whole file it was cut from, for the server to colour")
{
	TempDirectory dir;
	const auto file = dir.GetPath() / "Layer.h";
	Write(file, "#pragma once\nint X;\n");

	Core::DocumentSet documents;
	const DefinitionPreview preview = LoadDefinitionPreview(documents, file, 1);

	CHECK(preview.Text.Materialize() == "#pragma once\nint X;\n");
}

TEST_CASE("An open document wins over the file on disk")
{
	TempDirectory dir;
	const auto file = dir.GetPath() / "Layer.h";
	Write(file, "int Old;\n");

	Core::DocumentSet documents;
	REQUIRE(documents.Open(file, true) == Core::OpenOutcome::Opened);

	// An unsaved edit: the buffer now reads "int New;" while the disk still says "int Old;".
	Core::OpenItem& item = documents.At(*documents.Find(file));
	item.Caret.Anchor = 4;
	item.Caret.Offset = 7;
	REQUIRE(Core::InsertText(item, "New"));

	const DefinitionPreview preview = LoadDefinitionPreview(documents, file, 0);
	CHECK(preview.Lines.front() == "int New;");

	// The tab colours an open document; the preview keeps no copy of it.
	CHECK(preview.Text.Materialize().empty());
}

TEST_CASE("A UTF-16 file on disk is decoded")
{
	TempDirectory dir;
	const auto file = dir.GetPath() / "Wide.h";
	// "int X;\n" in UTF-16LE with a BOM.
	const std::string bytes{"\xFF\xFE" "i\0n\0t\0 \0X\0;\0\n\0", 16};
	Write(file, bytes);

	Core::DocumentSet documents;
	const DefinitionPreview preview = LoadDefinitionPreview(documents, file, 0);

	REQUIRE_FALSE(preview.Unreadable);
	CHECK(preview.Lines == std::vector<std::string>{"int X;"});
}

TEST_CASE("A file at the size limit is not read")
{
	TempDirectory dir;
	const auto file = dir.GetPath() / "Huge.h";
	Write(file, std::string(Graphite::kPreviewFileByteLimit, 'x'));

	Core::DocumentSet documents;
	const DefinitionPreview preview = LoadDefinitionPreview(documents, file, 0);

	CHECK(preview.TooLarge);
	CHECK(preview.Lines.empty());
}

TEST_CASE("A missing file is unreadable, not an exception")
{
	Core::DocumentSet documents;
	CHECK(LoadDefinitionPreview(documents, "C:/does/not/exist.h", 0).Unreadable);
}

TEST_CASE("Only a preview with lines, or one too large to read, has anything to draw")
{
	DefinitionPreview lines;
	lines.Lines = {"int X;"};
	CHECK(Graphite::IsDrawable(lines));

	DefinitionPreview tooLarge;
	tooLarge.TooLarge = true;
	CHECK(Graphite::IsDrawable(tooLarge));

	DefinitionPreview unreadable;
	unreadable.Unreadable = true;
	CHECK_FALSE(Graphite::IsDrawable(unreadable));

	// An out-of-range line: no lines and no flag.
	CHECK_FALSE(Graphite::IsDrawable(DefinitionPreview{}));
}
