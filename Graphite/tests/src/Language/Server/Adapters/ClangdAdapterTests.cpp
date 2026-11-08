#include <doctest/doctest.h>

#include "Language/Server/Adapters/ClangdAdapter.h"
#include "TempDirectory.h"

#include <fstream>
#include <optional>

namespace {

	void Touch(const std::filesystem::path& path)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream{path} << "";
	}
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

TEST_CASE("clangd reports a project with no compile database as incomplete and says what is missing")
{
	TempDirectory temp;
	const Graphite::ServerAdapter& clangd = Graphite::AdapterOf(Graphite::AdapterKind::Clangd);

	const std::optional<Graphite::ProjectProblem> problem = clangd.ProjectProblemAt(Graphite::AdapterContext{.Root = temp.GetPath()});

	REQUIRE(problem.has_value());
	CHECK(problem->Summary == "no compile database");
	CHECK(problem->Explanation.find("compile_commands.json") != std::string::npos);
	CHECK(problem->Explanation.find("compile_flags.txt") != std::string::npos);
}

TEST_CASE("clangd reports nothing missing once a compile database is there")
{
	TempDirectory temp;
	Touch(temp.GetPath() / "compile_commands.json");

	CHECK_FALSE(Graphite::AdapterOf(Graphite::AdapterKind::Clangd).ProjectProblemAt(Graphite::AdapterContext{.Root = temp.GetPath()}).has_value());
}

TEST_CASE("clangd answers every configuration section with an empty object")
{
	const Graphite::ServerAdapter& clangd = Graphite::AdapterOf(Graphite::AdapterKind::Clangd);
	const Graphite::AdapterContext context{.Root = {}, .InitializationOptions = nullptr, .Settings = nlohmann::json{{"clangd", 1}}};

	CHECK(clangd.WorkspaceConfiguration(context, "clangd") == nlohmann::json::object());
}

TEST_CASE("clangd needs a document opened before it indexes and can switch between header and source")
{
	const Graphite::ServerAdapter& clangd = Graphite::AdapterOf(Graphite::AdapterKind::Clangd);

	CHECK(clangd.WarmUpNeedsOpen());
	CHECK(clangd.SupportsSwitchSourceHeader());
}

TEST_CASE("clangd with compile_flags.txt alone is reported as seeing only the open files")
{
	TempDirectory temp;
	Touch(temp.GetPath() / "compile_flags.txt");

	const std::optional<Graphite::ProjectProblem> problem =
		Graphite::AdapterOf(Graphite::AdapterKind::Clangd).ProjectProblemAt(Graphite::AdapterContext{.Root = temp.GetPath()});

	REQUIRE(problem.has_value());
	CHECK(problem->Summary == "open files only");
	CHECK(problem->Explanation.find("compile_commands.json") != std::string::npos);
}

TEST_CASE("clangd with compile_flags.txt beside a compile_commands.json or a .clangd reports nothing missing")
{
	TempDirectory withCommands;
	Touch(withCommands.GetPath() / "compile_flags.txt");
	Touch(withCommands.GetPath() / "build" / "compile_commands.json");

	TempDirectory withClangd;
	Touch(withClangd.GetPath() / "compile_flags.txt");
	Touch(withClangd.GetPath() / ".clangd");

	const Graphite::ServerAdapter& clangd = Graphite::AdapterOf(Graphite::AdapterKind::Clangd);
	CHECK_FALSE(clangd.ProjectProblemAt(Graphite::AdapterContext{.Root = withCommands.GetPath()}).has_value());
	CHECK_FALSE(clangd.ProjectProblemAt(Graphite::AdapterContext{.Root = withClangd.GetPath()}).has_value());
}
