#include <doctest/doctest.h>

#include "Language/Server/DotnetRuntime.h"
#include "TempDirectory.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Platform/Process.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

	void Touch(const std::filesystem::path& path)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream{path} << "";
	}

	std::filesystem::path MakeDotnetRoot(const std::filesystem::path& root, std::initializer_list<std::string_view> runtimes)
	{
		Touch(root / "dotnet.exe");

		for (const std::string_view runtime : runtimes)
		{
			Touch(root / "shared" / "Microsoft.NETCore.App" / runtime / "System.Private.CoreLib.dll");
		}

		return root;
	}
}

TEST_CASE("A dotnet root lists its runtimes highest first")
{
	TempDirectory temp;
	const std::filesystem::path root =
		MakeDotnetRoot(temp.GetPath() / "dotnet", {"8.0.11", "10.0.10", "10.0.7", "9.0.0-rc.1.24431.7"});
	const std::array<std::filesystem::path, 1> candidates{root};

	const std::vector<Graphite::DotnetInstallation> found = Graphite::FindDotnetInstallations(candidates);

	REQUIRE(found.size() == 1);
	CHECK(found[0].Executable == root / "dotnet.exe");
	CHECK(found[0].RuntimeVersions == std::vector<std::string>{"10.0.10", "10.0.7", "9.0.0-rc.1.24431.7", "8.0.11"});
}

TEST_CASE("A candidate without dotnet or without a runtime is skipped and a root given twice counts once")
{
	TempDirectory temp;
	const std::filesystem::path nothing = temp.GetPath() / "nothing";
	std::filesystem::create_directories(nothing);
	Touch(temp.GetPath() / "muxer-only" / "dotnet.exe");
	const std::filesystem::path real = MakeDotnetRoot(temp.GetPath() / Core::FromUtf8("Program Files Ω") / "dotnet", {"10.0.10"});
	const std::array<std::filesystem::path, 4> candidates{nothing, temp.GetPath() / "muxer-only", real, real};

	const std::vector<Graphite::DotnetInstallation> found = Graphite::FindDotnetInstallations(candidates);

	REQUIRE(found.size() == 1);
	CHECK(found[0].Executable == real / "dotnet.exe");
}

TEST_CASE("A directory under the runtimes that is not a version is not a runtime")
{
	TempDirectory temp;
	const std::filesystem::path root = MakeDotnetRoot(temp.GetPath() / "dotnet", {"10.0.10"});
	std::filesystem::create_directories(root / "shared" / "Microsoft.NETCore.App" / "backup");
	Touch(root / "shared" / "Microsoft.NETCore.App" / "11.0.0.txt");
	const std::array<std::filesystem::path, 1> candidates{root};

	const std::vector<Graphite::DotnetInstallation> found = Graphite::FindDotnetInstallations(candidates);

	REQUIRE(found.size() == 1);
	CHECK(found[0].RuntimeVersions == std::vector<std::string>{"10.0.10"});
}

TEST_CASE("Only an installation with a runtime of the needed major or newer qualifies")
{
	const Graphite::DotnetInstallation old{.Executable = "C:/old/dotnet.exe", .RuntimeVersions = {"8.0.11"}};
	const Graphite::DotnetInstallation current{.Executable = "C:/new/dotnet.exe", .RuntimeVersions = {"10.0.10", "8.0.11"}};
	const std::array<Graphite::DotnetInstallation, 2> installations{old, current};

	CHECK_FALSE(Graphite::HasRuntimeAtLeast(old, 10));
	CHECK(Graphite::HasRuntimeAtLeast(old, 8));
	CHECK(Graphite::HasRuntimeAtLeast(current, 10));
	CHECK(Graphite::FirstWithRuntimeAtLeast(installations, 10) == &installations[1]);
	CHECK(Graphite::FirstWithRuntimeAtLeast(installations, 8) == &installations[0]);
	CHECK(Graphite::FirstWithRuntimeAtLeast(installations, 11) == nullptr);
}

TEST_CASE("The last place dotnet is looked for is under Program Files")
{
	const std::optional<std::filesystem::path> programFiles = Core::EnvironmentPath(L"ProgramFiles");
	REQUIRE(programFiles);

	const std::vector<std::filesystem::path> roots = Graphite::DotnetCandidateRoots();

	REQUIRE_FALSE(roots.empty());
	CHECK(roots.back() == *programFiles / "dotnet");
}

TEST_CASE("The download page is the one for the major version needed")
{
	CHECK(Graphite::DotnetDownloadUrl(10) == "https://dotnet.microsoft.com/download/dotnet/10.0");
}
