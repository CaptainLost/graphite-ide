#include <doctest/doctest.h>

#include "Lang/Server/ServerRegistry.h"
#include "TempDirectory.h"

#include <fstream>

namespace {

	void Touch(const std::filesystem::path& path)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream{path} << "";
	}
}

TEST_CASE("ServerConfigFor knows C++ and nothing else yet")
{
	REQUIRE(Graphite::ServerConfigFor(Graphite::LanguageId::Cpp) != nullptr);
	CHECK(Graphite::ServerConfigFor(Graphite::LanguageId::Cpp)->Command == "clangd");
	CHECK(Graphite::ServerConfigFor(Graphite::LanguageId::Cpp)->ProtocolLanguageId == "cpp");
	CHECK(Graphite::ServerConfigFor(Graphite::LanguageId::PlainText) == nullptr);
}

TEST_CASE("RootFor climbs to the nearest directory holding a marker")
{
	TempDirectory temp;

	Touch(temp.GetPath() / "project" / "compile_flags.txt");
	Touch(temp.GetPath() / "project" / "src" / "deep" / "main.cpp");

	const auto* config = Graphite::ServerConfigFor(Graphite::LanguageId::Cpp);

	CHECK(Graphite::RootFor(*config, temp.GetPath() / "project" / "src" / "deep" / "main.cpp",
							temp.GetPath()) == temp.GetPath() / "project");
}

TEST_CASE("RootFor falls back when no marker is anywhere above the file")
{
	TempDirectory temp;

	Touch(temp.GetPath() / "loose" / "main.cpp");

	const auto* config = Graphite::ServerConfigFor(Graphite::LanguageId::Cpp);

	CHECK(Graphite::RootFor(*config, temp.GetPath() / "loose" / "main.cpp", temp.GetPath()) ==
		  temp.GetPath());
}

TEST_CASE("LacksCompileDatabase sees each accepted form")
{
	TempDirectory temp;

	CHECK(Graphite::LacksCompileDatabase(temp.GetPath()));

	Touch(temp.GetPath() / "compile_flags.txt");
	CHECK_FALSE(Graphite::LacksCompileDatabase(temp.GetPath()));
}

TEST_CASE("LacksCompileDatabase finds compile_commands.json in build/ too")
{
	TempDirectory temp;

	Touch(temp.GetPath() / "build" / "compile_commands.json");
	CHECK_FALSE(Graphite::LacksCompileDatabase(temp.GetPath()));
}

TEST_CASE("FindServerBinary prefers a configured path that exists")
{
	TempDirectory temp;
	const std::filesystem::path configured = temp.GetPath() / "clangd.exe";

	Touch(configured);

	const auto found = Graphite::FindServerBinary("clangd", configured);

	REQUIRE(found);
	CHECK(found.GetValue() == configured);
}

TEST_CASE("FindServerBinary reports not found rather than an empty path")
{
	const auto found = Graphite::FindServerBinary("no-such-server-8a3f", {});

	CHECK_FALSE(found);
	CHECK(found.GetError().Code == Core::ErrorCode::NotFound);
}
