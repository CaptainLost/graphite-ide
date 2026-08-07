#include <doctest/doctest.h>

#include "Core/Base/JsonFile.h"

#include "TempDirectory.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using core::JsonFile;

TEST_CASE("Loading a missing file yields an empty set of values")
{
    TempDirectory temp;

    const JsonFile file = JsonFile::Load(temp.Path() / "absent.json");

    CHECK(file.Count() == 0);
    CHECK(file.Get("anything").empty());
}

TEST_CASE("A value survives a save and load round trip")
{
    TempDirectory temp;
    const auto path = temp.Path() / "settings.json";

    JsonFile written;
    written.Set("lastFolder", "D:\\Repos\\IDE");
    REQUIRE(written.Save(path));

    const JsonFile read = JsonFile::Load(path);

    CHECK(read.Count() == 1);
    CHECK(read.Get("lastFolder") == "D:\\Repos\\IDE");
}

TEST_CASE("Writing one key preserves the others")
{
    TempDirectory temp;
    const auto path = temp.Path() / "settings.json";

    JsonFile first;
    first.Set("lastFolder", "D:\\Repos\\IDE");
    first.Set("ignoredDisplay", "dim");
    REQUIRE(first.Save(path));

    JsonFile second = JsonFile::Load(path);
    second.Set("ignoredDisplay", "hide");
    REQUIRE(second.Save(path));

    const JsonFile read = JsonFile::Load(path);

    CHECK(read.Get("lastFolder") == "D:\\Repos\\IDE");
    CHECK(read.Get("ignoredDisplay") == "hide");
}

TEST_CASE("Setting an existing key replaces it rather than duplicating it")
{
    JsonFile file;
    file.Set("mode", "dim");
    file.Set("mode", "hide");

    CHECK(file.Count() == 1);
    CHECK(file.Get("mode") == "hide");
}

TEST_CASE("An empty value is stored and reads back empty")
{
    TempDirectory temp;
    const auto path = temp.Path() / "settings.json";

    JsonFile written;
    written.Set("lastFolder", "");
    REQUIRE(written.Save(path));

    const JsonFile read = JsonFile::Load(path);

    CHECK(read.Count() == 1);
    CHECK(read.Get("lastFolder").empty());
}

TEST_CASE("A value containing a newline survives intact")
{
    TempDirectory temp;
    const auto path = temp.Path() / "settings.json";

    JsonFile written;
    written.Set("expression", "a\nb");
    REQUIRE(written.Save(path));

    const JsonFile read = JsonFile::Load(path);

    CHECK(read.Get("expression") == "a\nb");
}

TEST_CASE("A value containing an equals sign survives intact")
{
    TempDirectory temp;
    const auto path = temp.Path() / "settings.json";

    JsonFile written;
    written.Set("expression", "a=b=c");
    REQUIRE(written.Save(path));

    CHECK(JsonFile::Load(path).Get("expression") == "a=b=c");
}

TEST_CASE("A malformed file loads as empty rather than throwing")
{
    TempDirectory temp;
    const auto path = temp.MakeFile("settings.json", "{ this is not valid json ");

    const JsonFile read = JsonFile::Load(path);

    CHECK(read.Count() == 0);
    CHECK(read.Get("anything").empty());
}

TEST_CASE("A non-string value reads back as empty rather than throwing")
{
    TempDirectory temp;
    const auto path = temp.MakeFile("settings.json", "{ \"count\": 42 }");

    const JsonFile read = JsonFile::Load(path);

    CHECK(read.Get("count").empty());
}

TEST_CASE("Saving is human-readable, not a single-line blob")
{
    TempDirectory temp;
    const auto path = temp.Path() / "settings.json";

    JsonFile written;
    written.Set("lastFolder", "D:\\Repos\\IDE");
    REQUIRE(written.Save(path));

    std::ifstream stream{path};
    std::string contents{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};

    CHECK(contents.find('\n') != std::string::npos);
}

TEST_CASE("A failed save leaves the previous file intact")
{
    TempDirectory temp;
    const auto path = temp.Path() / "settings.json";

    JsonFile original;
    original.Set("lastFolder", "D:\\Repos\\IDE");
    REQUIRE(original.Save(path));

    JsonFile doomed;
    doomed.Set("lastFolder", "D:\\Elsewhere");
    CHECK_FALSE(doomed.Save(temp.Path() / "missing-directory" / "settings.json"));

    // The unrelated file must be untouched, and no stray temporary left behind.
    CHECK(JsonFile::Load(path).Get("lastFolder") == "D:\\Repos\\IDE");
    CHECK_FALSE(std::filesystem::exists(temp.Path() / "settings.json.tmp"));
}
