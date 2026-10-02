#include <doctest/doctest.h>

#include "Lang/Server/ServerRegistry.h"
#include "TempDirectory.h"

#include "Core/Platform/Process.h"

#include <cstdlib>
#include <fstream>
#include <optional>

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

// clangd reads build/compile_commands.json under every ancestor of a file, so a directory
// holding one is that file's project - and the trust check has to judge the directory the
// server will read from, not fall back to a root it would otherwise never be given.
TEST_CASE("RootFor treats a compile database in build/ as a project marker")
{
	TempDirectory temp;

	Touch(temp.GetPath() / "project" / "build" / "compile_commands.json");
	Touch(temp.GetPath() / "project" / "src" / "main.cpp");

	const auto* config = Graphite::ServerConfigFor(Graphite::LanguageId::Cpp);

	CHECK(Graphite::RootFor(*config, temp.GetPath() / "project" / "src" / "main.cpp", {}) ==
		  temp.GetPath() / "project");
	CHECK(Graphite::IsProjectRoot(temp.GetPath() / "project"));
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

// PATH read through the narrow API turns every character outside the ANSI code page into '?',
// so a server installed under such a directory was never found.
TEST_CASE("FindServerBinary finds a server in a directory on PATH whose name is outside the ANSI code page")
{
	namespace fs = std::filesystem;
	const fs::path directory = fs::temp_directory_path() / L"graphite-中文-\U0001F600-server";
	std::error_code status;
	fs::create_directories(directory, status);
	REQUIRE_FALSE(status);

	Touch(directory / L"graphite-wide-server-probe.exe");

	const std::optional<fs::path> original = Core::EnvironmentPath(L"PATH");
	REQUIRE(original.has_value());
	REQUIRE(_wputenv_s(L"PATH", directory.wstring().c_str()) == 0);

	const auto found = Graphite::FindServerBinary("graphite-wide-server-probe", {});

	// Restore before asserting so a failure cannot leave later tests with a broken PATH.
	const errno_t restored = _wputenv_s(L"PATH", original->wstring().c_str());
	fs::remove_all(directory, status);

	CHECK(restored == 0);
	REQUIRE(found);
	CHECK(found.GetValue() == directory / L"graphite-wide-server-probe.exe");
}

TEST_CASE("FindServerBinary reports not found rather than an empty path")
{
	const auto found = Graphite::FindServerBinary("no-such-server-8a3f", {});

	CHECK_FALSE(found);
	CHECK(found.GetError().Code == Core::ErrorCode::NotFound);
}
