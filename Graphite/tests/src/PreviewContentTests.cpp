#include <doctest/doctest.h>

#include "Search/PreviewContent.h"

#include "Core/Text/DocumentSet.h"

#include "TempDirectory.h"

#include <fstream>
#include <string>

using namespace Graphite;

TEST_CASE("A file that is not open is read from disk")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("a.cpp", "int x;\nint y;\n");

	Core::DocumentSet documents;
	PreviewContent content;

	content.Show(file, documents);

	REQUIRE(content.GetState() == PreviewState::Ready);
	REQUIRE(content.GetBuffer() != nullptr);
	CHECK(content.GetBuffer()->Materialize() == "int x;\nint y;\n");
}

TEST_CASE("An open document's buffer wins over the file on disk")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("a.cpp", "on disk\n");

	Core::DocumentSet documents;

	REQUIRE(documents.Open(file, true) == Core::OpenOutcome::Opened);

	const auto index = documents.Find(file);
	REQUIRE(index.has_value());

	Core::OpenItem& item = documents.At(*index);
	REQUIRE(item.Document.has_value());

	// The unsaved edit the preview has to show. Insert rather than replace, so the test is
	// about which source is read and not about how a buffer is rebuilt.
	item.Document->GetBuffer().Insert(0, "edited ");

	PreviewContent content;
	content.Show(file, documents);

	REQUIRE(content.GetState() == PreviewState::Ready);
	REQUIRE(content.GetBuffer() != nullptr);
	CHECK(content.GetBuffer()->Materialize() == "edited on disk\n");
	CHECK(content.GetBuffer() == &item.Document->GetBuffer());
}

TEST_CASE("A file that has gone reports itself unreadable rather than empty")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.GetPath() / "never-existed.cpp";

	Core::DocumentSet documents;
	PreviewContent content;

	content.Show(file, documents);

	CHECK(content.GetState() == PreviewState::Unreadable);
	CHECK_FALSE(content.GetMessage().empty());
	CHECK(content.GetBuffer() == nullptr);
}

TEST_CASE("A binary file is skipped rather than shown")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.GetPath() / "blob.bin";

	{
		std::ofstream stream{file, std::ios::binary | std::ios::trunc};
		const std::string bytes("\0\1\2\0\3binary", 11);
		stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	}

	Core::DocumentSet documents;
	PreviewContent content;

	content.Show(file, documents);

	CHECK(content.GetState() == PreviewState::Skipped);
	CHECK_FALSE(content.GetMessage().empty());
	CHECK(content.GetBuffer() == nullptr);
}

TEST_CASE("Showing a second file replaces the first")
{
	TempDirectory temp;
	const std::filesystem::path first = temp.MakeFile("first.cpp", "first\n");
	const std::filesystem::path second = temp.MakeFile("second.cpp", "second\n");

	Core::DocumentSet documents;
	PreviewContent content;

	content.Show(first, documents);
	content.Show(second, documents);

	REQUIRE(content.GetState() == PreviewState::Ready);
	CHECK(content.GetPath() == second);
	CHECK(content.GetBuffer()->Materialize() == "second\n");
}

TEST_CASE("A file closed while its preview is showing falls back to disk")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("a.cpp", "on disk\n");

	Core::DocumentSet documents;

	REQUIRE(documents.Open(file, true) == Core::OpenOutcome::Opened);

	const auto index = documents.Find(file);
	REQUIRE(index.has_value());

	PreviewContent content;
	content.Show(file, documents);

	REQUIRE(content.GetState() == PreviewState::Ready);

	documents.Close(*index);
	content.Show(file, documents);

	REQUIRE(content.GetState() == PreviewState::Ready);
	CHECK(content.GetBuffer()->Materialize() == "on disk\n");
}

TEST_CASE("Clear forgets the file")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("a.cpp", "text\n");

	Core::DocumentSet documents;
	PreviewContent content;

	content.Show(file, documents);
	content.Clear();

	CHECK(content.GetState() == PreviewState::Empty);
	CHECK(content.GetBuffer() == nullptr);
	CHECK(content.GetPath().empty());
}
