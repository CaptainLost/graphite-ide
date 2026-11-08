#include <doctest/doctest.h>

#include "Git/GitIgnoreRules.h"

#include "Core/Git/Repository.h"
#include "TempDirectory.h"

#include <filesystem>
#include <fstream>

namespace {

	void Touch(const std::filesystem::path& path)
	{
		std::filesystem::create_directories(path.parent_path());

		std::ofstream file(path, std::ios::binary);
		file << "x";
	}
}

TEST_CASE("Git's ignore rules ignore what the repository's .gitignore names and nothing else")
{
	TempDirectory folder;
	REQUIRE(Core::Repository::Init(folder.GetPath()));
	Touch(folder.GetPath() / "kept.txt");
	{
		std::ofstream rules(folder.GetPath() / ".gitignore", std::ios::binary);
		rules << "*.log\n";
	}

	const Graphite::GitIgnoreRules rules{folder.GetPath()};

	CHECK(rules.IsIgnored(folder.GetPath() / "build.log"));
	CHECK_FALSE(rules.IsIgnored(folder.GetPath() / "kept.txt"));
}

TEST_CASE("Outside any repository git's ignore rules ignore nothing")
{
	TempDirectory folder;
	Touch(folder.GetPath() / "build.log");

	const Graphite::GitIgnoreRules rules{folder.GetPath()};

	CHECK_FALSE(rules.IsIgnored(folder.GetPath() / "build.log"));
}