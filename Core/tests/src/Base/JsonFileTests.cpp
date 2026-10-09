#include <doctest/doctest.h>

#include "Core/Base/JsonFile.h"

#include "TempDirectory.h"

#include <algorithm>
#include <filesystem>
#include <utility>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using Core::JsonFile;

namespace {
	std::vector<std::filesystem::path> ListEntryNames(const std::filesystem::path& folder)
	{
		std::vector<std::filesystem::path> entryNames;

		for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator{folder})
		{
			entryNames.push_back(entry.path().filename());
		}

		return entryNames;
	}
}

TEST_CASE("Loading a missing file yields an empty set of values")
{
	TempDirectory temporaryDirectory;

	const JsonFile file = JsonFile::Load(temporaryDirectory.GetPath() / "absent.json");

	CHECK(file.GetCount() == 0);
	CHECK(file.Get("anything").empty());
}

TEST_CASE("A value survives a save and load round trip")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path path = temporaryDirectory.GetPath() / "settings.json";

	JsonFile written;
	written.Set("lastFolder", "D:\\Repos\\IDE");
	REQUIRE(written.Save(path));

	const JsonFile read = JsonFile::Load(path);

	CHECK(read.GetCount() == 1);
	CHECK(read.Get("lastFolder") == "D:\\Repos\\IDE");
}

TEST_CASE("Writing one key preserves the others")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path path = temporaryDirectory.GetPath() / "settings.json";

	JsonFile firstWriter;
	firstWriter.Set("lastFolder", "D:\\Repos\\IDE");
	firstWriter.Set("ignoredDisplay", "dim");
	REQUIRE(firstWriter.Save(path));

	JsonFile secondWriter = JsonFile::Load(path);
	secondWriter.Set("ignoredDisplay", "hide");
	REQUIRE(secondWriter.Save(path));

	const JsonFile read = JsonFile::Load(path);

	CHECK(read.Get("lastFolder") == "D:\\Repos\\IDE");
	CHECK(read.Get("ignoredDisplay") == "hide");
}

TEST_CASE("Setting an existing key replaces it rather than duplicating it")
{
	JsonFile file;
	file.Set("mode", "dim");
	file.Set("mode", "hide");

	CHECK(file.GetCount() == 1);
	CHECK(file.Get("mode") == "hide");
}

TEST_CASE("An empty value is stored and reads back empty")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path path = temporaryDirectory.GetPath() / "settings.json";

	JsonFile written;
	written.Set("lastFolder", "");
	REQUIRE(written.Save(path));

	const JsonFile read = JsonFile::Load(path);

	CHECK(read.GetCount() == 1);
	CHECK(read.Get("lastFolder").empty());
}

TEST_CASE("A value containing a newline survives intact")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path path = temporaryDirectory.GetPath() / "settings.json";

	JsonFile written;
	written.Set("expression", "a\nb");
	REQUIRE(written.Save(path));

	const JsonFile read = JsonFile::Load(path);

	CHECK(read.Get("expression") == "a\nb");
}

TEST_CASE("A value containing an equals sign survives intact")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path path = temporaryDirectory.GetPath() / "settings.json";

	JsonFile written;
	written.Set("expression", "a=b=c");
	REQUIRE(written.Save(path));

	CHECK(JsonFile::Load(path).Get("expression") == "a=b=c");
}

TEST_CASE("A malformed file loads as empty rather than throwing")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path path = temporaryDirectory.MakeFile("settings.json", "{ this is not valid json ");

	const JsonFile read = JsonFile::Load(path);

	CHECK(read.GetCount() == 0);
	CHECK(read.Get("anything").empty());
}

TEST_CASE("A file that is not a JSON object is reported as malformed, and a missing or blank one is not")
{
	TempDirectory temporaryDirectory;

	CHECK(JsonFile::Load(temporaryDirectory.MakeFile("broken.json", "{ this is not valid json ")).IsMalformed());
	CHECK(JsonFile::Load(temporaryDirectory.MakeFile("array.json", "[1, 2]")).IsMalformed());

	CHECK_FALSE(JsonFile::Load(temporaryDirectory.GetPath() / "missing.json").IsMalformed());
	CHECK_FALSE(JsonFile::Load(temporaryDirectory.MakeFile("blank.json", " \r\n")).IsMalformed());
	CHECK_FALSE(JsonFile::Load(temporaryDirectory.MakeFile("good.json", R"({"a": "1"})")).IsMalformed());
	CHECK_FALSE(JsonFile{}.IsMalformed());
}

TEST_CASE("A non-string value reads back as empty rather than throwing")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path path = temporaryDirectory.MakeFile("settings.json", "{ \"count\": 42, \"mode\": \"dim\" }");

	const JsonFile read = JsonFile::Load(path);

	CHECK(read.Get("count").empty());

	// Without this check the test would also pass if parsing had failed and everything read as empty.
	CHECK(read.Get("mode") == "dim");
}

TEST_CASE("Saving is human-readable, not a single-line blob")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path path = temporaryDirectory.GetPath() / "settings.json";

	JsonFile written;
	written.Set("lastFolder", "D:\\Repos\\IDE");
	REQUIRE(written.Save(path));

	std::ifstream stream{path};
	std::string contents{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};

	CHECK(contents.find('\n') != std::string::npos);
}

TEST_CASE("A save that fails leaves an unrelated file and the folder around it untouched")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path unrelatedFile = temporaryDirectory.GetPath() / "settings.json";

	JsonFile original;
	original.Set("lastFolder", "D:\\Repos\\IDE");
	REQUIRE(original.Save(unrelatedFile));

	const std::filesystem::path targetInMissingDirectory = temporaryDirectory.GetPath() / "missing-directory" / "settings.json";
	JsonFile doomed;
	doomed.Set("lastFolder", "D:\\Elsewhere");
	CHECK_FALSE(doomed.Save(targetInMissingDirectory));

	const std::vector<std::filesystem::path> onlyTheUnrelatedFile = {unrelatedFile.filename()};
	CHECK(JsonFile::Load(unrelatedFile).Get("lastFolder") == "D:\\Repos\\IDE");
	CHECK_FALSE(std::filesystem::exists(targetInMissingDirectory.parent_path()));
	CHECK(ListEntryNames(temporaryDirectory.GetPath()) == onlyTheUnrelatedFile);
}

TEST_CASE("A value that is not valid UTF-8 is saved instead of throwing")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path path = temporaryDirectory.GetPath() / "settings.json";

	JsonFile written;
	written.Set("broken", std::string{"aÿþb"});

	REQUIRE(written.Save(path));
	CHECK(std::filesystem::exists(path));

	const std::filesystem::path leftoverTemporaryFile = temporaryDirectory.GetPath() / "settings.json.tmp";
	CHECK_FALSE(std::filesystem::exists(leftoverTemporaryFile));

	const JsonFile read = JsonFile::Load(path);
	CHECK_FALSE(read.Get("broken").empty());
}

TEST_CASE("A moved-from file leaves the source usable enough to destroy")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path path = temporaryDirectory.GetPath() / "settings.json";

	JsonFile original;
	original.Set("lastFolder", "D:\\Repos\\IDE");

	const JsonFile moved = std::move(original);

	CHECK(moved.Get("lastFolder") == "D:\\Repos\\IDE");
	CHECK(moved.Save(path));
}

TEST_CASE("A moved-from file reads as empty and can be written to again")
{
	JsonFile original;
	original.Set("a", "1");

	JsonFile moved = std::move(original);

	CHECK(original.GetCount() == 0);
	CHECK(original.Get("a").empty());
	CHECK(original.CollectKeys().empty());
	CHECK_FALSE(original.IsMalformed());

	original.Set("b", "2");
	original.Remove("b");
	original.Set("c", "3");
	CHECK(original.Get("c") == "3");

	moved = std::move(original);
	CHECK(moved.Get("c") == "3");
	CHECK(original.GetCount() == 0);
}

TEST_CASE("CollectKeys lists what was stored, so a caller can find its own by prefix")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path path = temporaryDirectory.GetPath() / "settings.json";

	JsonFile file;
	file.Set("key.editor.zoomIn", "Ctrl+U");
	file.Set("wordWrap", "true");
	REQUIRE(file.Save(path));

	const JsonFile loaded = JsonFile::Load(path);
	std::vector<std::string> keys = loaded.CollectKeys();
	std::sort(keys.begin(), keys.end());

	REQUIRE(keys.size() == 2);
	CHECK(keys[0] == "key.editor.zoomIn");
	CHECK(keys[1] == "wordWrap");
}

TEST_CASE("CollectKeys on an empty file is empty")
{
	CHECK(JsonFile{}.CollectKeys().empty());
}

TEST_CASE("Remove takes the key out rather than leaving it holding an empty value")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path path = temporaryDirectory.GetPath() / "settings.json";

	JsonFile file;
	file.Set("key.editor.zoomIn", "Ctrl+U");
	file.Set("wordWrap", "true");
	file.Remove("key.editor.zoomIn");

	CHECK(file.GetCount() == 1);
	CHECK(file.Get("key.editor.zoomIn").empty());
	REQUIRE(file.Save(path));

	const JsonFile read = JsonFile::Load(path);
	CHECK(read.CollectKeys() == std::vector<std::string>{"wordWrap"});
}

TEST_CASE("Removing a key that is not there changes nothing")
{
	JsonFile file;
	file.Set("wordWrap", "true");
	file.Remove("absent");

	CHECK(file.GetCount() == 1);
	CHECK(file.Get("wordWrap") == "true");
}

TEST_CASE("A value set at a path is read back at that path after a save and a load")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path path = temporaryDirectory.GetPath() / "settings.json";
	const std::vector<std::string> choicePath{"languages", "CSharp", "language_server"};

	JsonFile written;
	written.SetJsonAt(choicePath, R"("roslyn")");
	REQUIRE(written.Save(path));

	const JsonFile read = JsonFile::Load(path);

	CHECK(read.GetJsonAt(choicePath) == std::optional<std::string>{R"("roslyn")"});
}

TEST_CASE("Setting a value at a path keeps the values beside it")
{
	JsonFile file;
	const std::vector<std::string> clangdPath{"lsp", "clangd", "binary", "path"};
	const std::vector<std::string> roslynPath{"lsp", "roslyn", "binary", "path"};

	file.SetJsonAt(clangdPath, R"("C:\\clangd.exe")");
	file.SetJsonAt(roslynPath, R"("C:\\roslyn.exe")");

	CHECK(file.GetJsonAt(clangdPath) == std::optional<std::string>{R"("C:\\clangd.exe")"});
	CHECK(file.GetJsonAt(roslynPath) == std::optional<std::string>{R"("C:\\roslyn.exe")"});
}

TEST_CASE("A path through a value that is not an object reads as absent")
{
	JsonFile file;
	file.Set("lsp", "fast");

	const std::vector<std::string> throughString{"lsp", "clangd"};

	CHECK_FALSE(file.GetJsonAt(throughString).has_value());
}

TEST_CASE("Leaves list every value that is not an object with members, with the keys that lead to it")
{
	JsonFile file;
	file.Set("a", "x");

	const std::vector<std::string> numberPath{"b", "c"};
	const std::vector<std::string> emptyObjectPath{"b", "d"};
	const std::vector<std::string> arrayPath{"e"};

	file.SetJsonAt(numberPath, "1");
	file.SetJsonAt(emptyObjectPath, "{}");
	file.SetJsonAt(arrayPath, "[1,2]");

	const std::vector<Core::JsonLeaf> expected{
		Core::JsonLeaf{.Path = {"a"}, .Json = R"("x")"},
		Core::JsonLeaf{.Path = {"b", "c"}, .Json = "1"},
		Core::JsonLeaf{.Path = {"b", "d"}, .Json = "{}"},
		Core::JsonLeaf{.Path = {"e"}, .Json = "[1,2]"},
	};

	CHECK(file.CollectLeaves() == expected);
}

TEST_CASE("Removing a value at a path drops the objects it leaves empty and nothing else")
{
	JsonFile file;
	const std::vector<std::string> onlyServerPath{"lsp", "clangd", "binary", "path"};
	const std::vector<std::string> keptPath{"languages", "Cpp", "language_server"};

	file.SetJsonAt(onlyServerPath, R"("C:\\clangd.exe")");
	file.SetJsonAt(keptPath, R"("clangd")");

	file.RemoveAt(onlyServerPath);

	const std::vector<std::string> serversPath{"lsp"};

	CHECK_FALSE(file.GetJsonAt(serversPath).has_value());
	CHECK(file.GetJsonAt(keptPath) == std::optional<std::string>{R"("clangd")"});
}

TEST_CASE("Removing a path that is not there leaves an empty object beside it alone")
{
	JsonFile file;
	const std::vector<std::string> emptyOptionsPath{"lsp", "clangd", "initialization_options"};
	const std::vector<std::string> absentPath{"lsp", "clangd", "initialization_options", "fallbackFlags"};

	file.SetJsonAt(emptyOptionsPath, "{}");
	file.RemoveAt(absentPath);

	CHECK(file.GetJsonAt(emptyOptionsPath) == std::optional<std::string>{"{}"});
}

TEST_CASE("Setting text that does not parse changes nothing")
{
	JsonFile file;
	const std::vector<std::string> path{"lsp", "clangd"};

	file.SetJsonAt(path, "{not json");

	CHECK(file.GetCount() == 0);
	CHECK_FALSE(file.GetJsonAt(path).has_value());
}

TEST_CASE("A flat key is a leaf whose path is that one key")
{
	JsonFile file;
	file.Set("lastFolder", "D:\\Repos\\IDE");

	const std::vector<Core::JsonLeaf> expected{Core::JsonLeaf{.Path = {"lastFolder"}, .Json = R"("D:\\Repos\\IDE")"}};

	CHECK(file.CollectLeaves() == expected);
}
