#include <doctest/doctest.h>

#include "Core/Text/DocumentSet.h"
#include "Core/Text/DocumentStore.h"

#include "FileFixtures.h"
#include "TempDirectory.h"

#include <filesystem>
#include <memory>

using namespace Core;

TEST_CASE("Acquiring a path twice gives the same document")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "a.cpp";
	WriteFileBytes(path, "int a;\n");
	DocumentStore store;

	Result<std::shared_ptr<TextDocument>> first = store.Acquire(path);
	Result<std::shared_ptr<TextDocument>> second = store.Acquire(path);

	REQUIRE(first);
	REQUIRE(second);
	CHECK(first.GetValue() == second.GetValue());
	CHECK(store.Find(path) == first.GetValue());
}

TEST_CASE("A document lives as long as one holder keeps it")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "a.cpp";
	WriteFileBytes(path, "int a;\n");
	DocumentStore store;

	std::shared_ptr<TextDocument> first = store.Acquire(path).GetValue();
	std::shared_ptr<TextDocument> second = store.Acquire(path).GetValue();

	CHECK_FALSE(IsLastHolder(first));

	second.reset();
	CHECK(IsLastHolder(first));
	CHECK(store.Find(path) != nullptr);

	first.reset();
	CHECK(store.Find(path) == nullptr);
	CHECK(store.GetDocuments().empty());
}

TEST_CASE("A document left with no holder is read afresh on the next acquire")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "a.cpp";
	WriteFileBytes(path, "old\n");
	DocumentStore store;

	{
		std::shared_ptr<TextDocument> held = store.Acquire(path).GetValue();
		held->GetBuffer().Insert(0, "unsaved ");
	}

	WriteFileBytes(path, "new\n");

	CHECK(store.Acquire(path).GetValue()->GetBuffer().Materialize() == "new\n");
}

TEST_CASE("Moving a folder moves the documents under it and keeps their unsaved text")
{
	TempDirectory directory;
	const std::filesystem::path from = directory.GetPath() / "old";
	const std::filesystem::path to = directory.GetPath() / "new";
	std::filesystem::create_directories(from);
	WriteFileBytes(from / "a.cpp", "int a;\n");
	DocumentStore store;

	std::shared_ptr<TextDocument> held = store.Acquire(from / "a.cpp").GetValue();
	held->GetBuffer().Insert(0, "// edited\n");

	store.RewritePaths(from, to);

	CHECK(store.Find(from / "a.cpp") == nullptr);
	REQUIRE(store.Find(to / "a.cpp") == held);
	CHECK(held->GetPath() == to / "a.cpp");
	CHECK(held->GetBuffer().Materialize() == "// edited\nint a;\n");
}

TEST_CASE("A tab's document is the store's document")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "a.cpp";
	WriteFileBytes(path, "int a;\n");
	DocumentSet documents;

	REQUIRE(documents.Open(path, TabKind::Permanent) == OpenOutcome::Opened);

	CHECK(documents.At(0).Document == documents.GetStore().Find(path));
	CHECK(IsLastHolder(documents.At(0).Document));

	documents.Close(0);

	CHECK(documents.GetStore().Find(path) == nullptr);
}

TEST_CASE("Replacing a preview tab releases the replaced document")
{
	TempDirectory directory;
	const std::filesystem::path first = directory.GetPath() / "a.cpp";
	const std::filesystem::path second = directory.GetPath() / "b.cpp";
	WriteFileBytes(first, "int a;\n");
	WriteFileBytes(second, "int b;\n");
	DocumentSet documents;

	REQUIRE(documents.Open(first, TabKind::Preview) == OpenOutcome::Opened);
	REQUIRE(documents.Open(second, TabKind::Preview) == OpenOutcome::Opened);

	CHECK(documents.GetStore().Find(first) == nullptr);
	CHECK(documents.GetStore().Find(second) != nullptr);
}
