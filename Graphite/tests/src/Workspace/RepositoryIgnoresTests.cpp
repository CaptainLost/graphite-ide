#include <doctest/doctest.h>

#include "Workspace/RepositoryIgnores.h"

#include "TempDirectory.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Repository.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

TEST_CASE("A local repository's ignores answer as the repository does")
{
	TempDirectory temp;
	REQUIRE(Core::Repository::Init(temp.GetPath()));
	std::ofstream(temp.GetPath() / ".gitignore") << "*.log\n";

	const Core::Result<Core::Scope<Graphite::RepositoryIgnores>> ignores = Graphite::OpenLocalRepositoryIgnores(temp.GetPath());

	REQUIRE(ignores.HasValue());
	CHECK(ignores.GetValue()->IsIgnored(temp.GetPath() / "build.log", false));
	CHECK_FALSE(ignores.GetValue()->IsIgnored(temp.GetPath() / "notes.txt", false));
	CHECK(Core::NormalizeDirectory(ignores.GetValue()->GetWorkingDirectory()) == Core::NormalizeDirectory(temp.GetPath()));
	const std::vector<std::filesystem::path> ruleFiles = ignores.GetValue()->RuleFilesOutside(temp.GetPath());
	CHECK(std::any_of(ruleFiles.begin(), ruleFiles.end(), [](const std::filesystem::path& file) { return file.filename() == "exclude"; }));
}

TEST_CASE("A folder outside any repository has no local ignores")
{
	TempDirectory temp;

	const Core::Result<Core::Scope<Graphite::RepositoryIgnores>> ignores = Graphite::OpenLocalRepositoryIgnores(temp.GetPath());

	REQUIRE_FALSE(ignores.HasValue());
	CHECK(ignores.GetError().Code == Core::ErrorCode::NotFound);
}
