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

		std::ofstream out(path, std::ios::binary);
		out << "x";
	}

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

// The property that makes this usable on a real repository: an ignored directory is never
// descended into, so its contents cost nothing rather than costing a filter each.
TEST_CASE("EnumerateFiles does not descend into an ignored directory")
{
	const TempDirectory temp;

	Touch(temp.GetPath() / "keep.cpp");
	Touch(temp.GetPath() / "bin" / "drop.cpp");
	Touch(temp.GetPath() / "bin" / "deep" / "alsodrop.cpp");

	int directoriesAsked = 0;

	const Graphite::IgnorePredicate ignored =
		[&directoriesAsked](const std::filesystem::path& path, bool isDirectory) {
			if (isDirectory)
			{
				++directoriesAsked;
			}

			return path.filename() == "bin";
		};

	const std::vector<std::string> names = NamesUnder(temp.GetPath(), ignored);

	// Checked before the file-count REQUIRE below on purpose: that REQUIRE is fatal, so if
	// pruning silently stopped working it would abort the test before this line ever ran,
	// hiding the more direct evidence that bin/deep was entered. bin was asked about once;
	// this only proves bin/deep was never asked about, since the predicate can't tell it apart
	// from an entry reached honestly by walking down from a non-pruned bin.
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

	for (int file = 0; file < 40; ++file)
	{
		Touch(temp.GetPath() / ("f" + std::to_string(file) + ".cpp"));
	}

	const Core::CancellationToken token;

	Graphite::EnumerationInput input;
	input.Root = temp.GetPath();

	int seen = 0;

	Graphite::EnumerateFiles(input, token,
							 [&](const std::filesystem::path&) {
								 ++seen;
								 token.Cancel();
							 });

	// Cancelled from inside the first callback, so the walk must stop rather than run to 40.
	CHECK(seen < 40);
}

TEST_CASE("EnumerateFiles survives a root that is not there")
{
	int seen = 0;

	Graphite::EnumerationInput input;
	input.Root = "Z:/no/such/place/at/all";

	Graphite::EnumerateFiles(input, Core::CancellationToken{},
							 [&seen](const std::filesystem::path&) { ++seen; });

	CHECK(seen == 0);
}

TEST_CASE("EnumerateProjectFiles reports what the projects declare")
{
	Graphite::ContentFolder nested;
	nested.Name = "src";
	nested.Files.push_back(Graphite::ContentFile{"b.cpp", "C:/p/src/b.cpp"});

	Graphite::Project project;
	project.DisplayName = "P";
	project.Content.Root.Files.push_back(Graphite::ContentFile{"a.cpp", "C:/p/a.cpp"});
	project.Content.Root.Folders.push_back(nested);

	Graphite::Folder inner;
	inner.Name = "Group";
	inner.Projects.push_back(project);

	Graphite::Solution solution;
	solution.Root.Folders.push_back(inner);

	std::vector<std::string> names;

	Graphite::EnumerateProjectFiles(solution,
									[&names](const std::filesystem::path& file) { names.push_back(file.filename().string()); });

	std::sort(names.begin(), names.end());

	REQUIRE(names.size() == 2);
	CHECK(names[0] == "a.cpp");
	CHECK(names[1] == "b.cpp");
}
