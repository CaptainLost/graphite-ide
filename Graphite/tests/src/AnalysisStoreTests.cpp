#include <doctest/doctest.h>

#include "Lang/Analysis/AnalysisStore.h"

#include "TempDirectory.h"

#include <chrono>
#include <fstream>
#include <string>
#include <thread>

using namespace Graphite;

namespace {

	CachedAnalysis WithDeclaration(std::string name, std::filesystem::file_time_type modifiedAt)
	{
		CachedAnalysis analysis;

		Declaration declaration;
		declaration.Name = std::move(name);
		declaration.Kind = DeclarationKind::Function;

		analysis.Declarations.push_back(std::move(declaration));
		analysis.ModifiedAt = modifiedAt;

		return analysis;
	}

	// The store writes on a worker, so a test that asserts on the disk has to wait for it. Bounded
	// rather than open-ended: a hang here should fail the case, not the suite.
	void WaitForWrites(AnalysisStore& store)
	{
		for (int attempt = 0; attempt < 200 && !store.IsQuiet(); ++attempt)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}

		REQUIRE(store.IsQuiet());
	}

	// Drives SetRoot's listing to completion the way the application does — by polling.
	void WaitUntilReady(AnalysisStore& store)
	{
		for (int attempt = 0; attempt < 200 && !store.IsReady(); ++attempt)
		{
			store.Poll();
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}

		REQUIRE(store.IsReady());
	}
}

TEST_CASE("An analysis written in one session is read back in the next")
{
	TempDirectory temp;
	const std::filesystem::path source = temp.MakeFile("a.cpp", "int x;");
	const std::filesystem::file_time_type stamp = std::filesystem::last_write_time(source);

	{
		AnalysisStore store;
		store.SetRoot(temp.GetPath());
		WaitUntilReady(store);

		store.Write(source, WithDeclaration("Draw", stamp));
		WaitForWrites(store);
	}

	AnalysisStore next;
	next.SetRoot(temp.GetPath());
	WaitUntilReady(next);

	CHECK(next.Has(source));

	const std::optional<CachedAnalysis> back = next.Read(source, stamp);

	REQUIRE(back.has_value());
	REQUIRE(back->Declarations.size() == 1);
	CHECK(back->Declarations[0].Name == "Draw");
}

TEST_CASE("An analysis of a file that has changed since is neither offered nor kept")
{
	TempDirectory temp;
	const std::filesystem::path source = temp.MakeFile("a.cpp", "int x;");
	const std::filesystem::file_time_type stamp = std::filesystem::last_write_time(source);

	{
		AnalysisStore store;
		store.SetRoot(temp.GetPath());
		WaitUntilReady(store);
		store.Write(source, WithDeclaration("Draw", stamp));
		WaitForWrites(store);
	}

	// The file moves on between sessions, as it would across a branch switch.
	std::filesystem::last_write_time(source, stamp + std::chrono::seconds(60));

	AnalysisStore next;
	next.SetRoot(temp.GetPath());
	WaitUntilReady(next);

	CHECK_FALSE(next.Has(source));
	CHECK_FALSE(next.Read(source, std::filesystem::last_write_time(source)).has_value());
}

TEST_CASE("An analysis of a file that is gone is pruned from the disk, not left behind")
{
	TempDirectory temp;
	const std::filesystem::path source = temp.MakeFile("a.cpp", "int x;");
	const std::filesystem::file_time_type stamp = std::filesystem::last_write_time(source);

	{
		AnalysisStore store;
		store.SetRoot(temp.GetPath());
		WaitUntilReady(store);
		store.Write(source, WithDeclaration("Draw", stamp));
		WaitForWrites(store);
	}

	std::filesystem::remove(source);

	AnalysisStore next;
	next.SetRoot(temp.GetPath());
	WaitUntilReady(next);

	CHECK_FALSE(next.Has(source));

	// Nothing may be left holding bytes for a file that no longer exists — the store has no size
	// cap, so an orphan is an orphan for the life of the project.
	std::size_t records = 0;

	for (const auto& entry : std::filesystem::directory_iterator{temp.GetPath() / ".graphite" / "analysis"})
	{
		if (entry.path().extension() == ".rec")
		{
			++records;
		}
	}

	CHECK(records == 0);
}

TEST_CASE("A corrupt index throws the whole store away rather than half-trusting it")
{
	TempDirectory temp;
	const std::filesystem::path source = temp.MakeFile("a.cpp", "int x;");
	const std::filesystem::file_time_type stamp = std::filesystem::last_write_time(source);

	{
		AnalysisStore store;
		store.SetRoot(temp.GetPath());
		WaitUntilReady(store);
		store.Write(source, WithDeclaration("Draw", stamp));
		WaitForWrites(store);
	}

	{
		std::ofstream index{temp.GetPath() / ".graphite" / "analysis" / "index.bin",
							std::ios::binary | std::ios::trunc};
		index << "not an index";
	}

	AnalysisStore next;
	next.SetRoot(temp.GetPath());
	WaitUntilReady(next);

	CHECK_FALSE(next.Has(source));
}

TEST_CASE("A store with no root is ready at once and answers nothing")
{
	AnalysisStore store;

	CHECK(store.IsReady());
	CHECK_FALSE(store.Has("a.cpp"));
	CHECK_FALSE(store.Read("a.cpp", std::filesystem::file_time_type{}).has_value());

	// Writing without a root must be a no-op rather than a path built from an empty root.
	store.Write("a.cpp", CachedAnalysis{});
	CHECK(store.IsQuiet());
}

TEST_CASE("A record whose bytes were corrupted is refused and the entry does not survive")
{
	TempDirectory temp;
	const std::filesystem::path source = temp.MakeFile("a.cpp", "int x;");
	const std::filesystem::file_time_type stamp = std::filesystem::last_write_time(source);

	AnalysisStore store;
	store.SetRoot(temp.GetPath());
	WaitUntilReady(store);
	store.Write(source, WithDeclaration("Draw", stamp));
	WaitForWrites(store);

	for (const auto& entry : std::filesystem::directory_iterator{temp.GetPath() / ".graphite" / "analysis"})
	{
		if (entry.path().extension() == ".rec")
		{
			std::ofstream record{entry.path(), std::ios::binary | std::ios::trunc};
			record << "rubbish";
		}
	}

	CHECK_FALSE(store.Read(source, stamp).has_value());
}
