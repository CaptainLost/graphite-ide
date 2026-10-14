#include <doctest/doctest.h>

#include "Search/FileEnumerator.h"
#include "Solution/SolutionModel.h"
#include "TempDirectory.h"

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

namespace {

	void Touch(const std::filesystem::path& path)
	{
		std::filesystem::create_directories(path.parent_path());

		std::ofstream file(path, std::ios::binary);
		file << "x";
	}

	struct IgnoreFolderNamedBin
	{
		int* DirectoriesAsked;

		bool operator()(const std::filesystem::path& path, bool isDirectory) const
		{
			if (isDirectory)
			{
				++*DirectoriesAsked;
			}

			return path.filename() == "bin";
		}
	};

	struct CancelOnFirstFile
	{
		const Core::CancellationToken* Token;
		int* FilesSeen;

		void operator()(const std::filesystem::path&) const
		{
			++*FilesSeen;
			Token->Cancel();
		}
	};

	std::vector<std::string> NamesUnder(const std::filesystem::path& root,
										Graphite::IgnorePredicate ignored = {})
	{
		Graphite::EnumerationInput input;
		input.Root = root;
		input.Ignored = std::move(ignored);

		std::vector<std::string> names;

		Graphite::EnumerateFiles(input, Core::CancellationToken{},
								 [&names](const std::filesystem::path& file) { names.push_back(file.filename().string()); });

		std::sort(names.begin(), names.end());

		return names;
	}
}

TEST_CASE("EnumerateFiles reaches every file under the root")
{
	const TempDirectory temp;

	Touch(temp.GetPath() / "a.cpp");
	Touch(temp.GetPath() / "sub" / "b.h");
	Touch(temp.GetPath() / "sub" / "deep" / "c.md");

	const std::vector<std::string> names = NamesUnder(temp.GetPath());

	REQUIRE(names.size() == 3);
	CHECK(names[0] == "a.cpp");
	CHECK(names[1] == "b.h");
	CHECK(names[2] == "c.md");
}

TEST_CASE("EnumerateFiles does not descend into an ignored directory")
{
	const TempDirectory temp;

	Touch(temp.GetPath() / "keep.cpp");
	Touch(temp.GetPath() / "bin" / "drop.cpp");
	Touch(temp.GetPath() / "bin" / "deep" / "alsodrop.cpp");

	int directoriesAsked = 0;

	const Graphite::IgnorePredicate ignored = IgnoreFolderNamedBin{&directoriesAsked};

	const std::vector<std::string> names = NamesUnder(temp.GetPath(), ignored);

	CHECK(directoriesAsked == 1);

	REQUIRE(names.size() == 1);
	CHECK(names[0] == "keep.cpp");
}

TEST_CASE("EnumerateFiles leaves out an ignored file")
{
	const TempDirectory temp;

	Touch(temp.GetPath() / "keep.cpp");
	Touch(temp.GetPath() / "drop.obj");

	const std::vector<std::string> names =
		NamesUnder(temp.GetPath(), [](const std::filesystem::path& path, bool) { return path.extension() == ".obj"; });

	REQUIRE(names.size() == 1);
	CHECK(names[0] == "keep.cpp");
}

TEST_CASE("EnumerateFiles stops when its token is cancelled")
{
	const TempDirectory temp;

	constexpr int kFileCount = 40;

	for (int fileNumber = 0; fileNumber < kFileCount; ++fileNumber)
	{
		Touch(temp.GetPath() / ("f" + std::to_string(fileNumber) + ".cpp"));
	}

	const Core::CancellationToken token;

	Graphite::EnumerationInput input;
	input.Root = temp.GetPath();

	int filesSeen = 0;

	Graphite::EnumerateFiles(input, token, CancelOnFirstFile{&token, &filesSeen});

	CHECK(filesSeen < kFileCount);
}

TEST_CASE("EnumerateFiles survives a root that is not there")
{
	int filesSeen = 0;

	Graphite::EnumerationInput input;
	input.Root = "Z:/no/such/place/at/all";

	Graphite::EnumerateFiles(input, Core::CancellationToken{},
							 [&filesSeen](const std::filesystem::path&) { ++filesSeen; });

	CHECK(filesSeen == 0);
}

TEST_CASE("EnumerateProjectFiles reports what the projects declare")
{
	Graphite::ContentFolder nestedFolder;
	nestedFolder.Name = "src";
	nestedFolder.Files.push_back(Graphite::ContentFile{"b.cpp", "C:/p/src/b.cpp"});

	Graphite::Project project;
	project.DisplayName = "P";
	project.Content.Root.Files.push_back(Graphite::ContentFile{"a.cpp", "C:/p/a.cpp"});
	project.Content.Root.Folders.push_back(nestedFolder);

	Graphite::Folder group;
	group.Name = "Group";
	group.Projects.push_back(project);

	Graphite::Solution solution;
	solution.Root.Folders.push_back(group);

	std::vector<std::string> names;

	Graphite::EnumerateProjectFiles(solution,
									[&names](const std::filesystem::path& file) { names.push_back(file.filename().string()); });

	std::sort(names.begin(), names.end());

	REQUIRE(names.size() == 2);
	CHECK(names[0] == "a.cpp");
	CHECK(names[1] == "b.cpp");
}
