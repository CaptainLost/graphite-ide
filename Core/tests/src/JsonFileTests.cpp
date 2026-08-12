#include <doctest/doctest.h>

#include "Core/Base/JsonFile.h"

#include "TempDirectory.h"

#include <algorithm>
#include <filesystem>
#include <utility>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using Core::JsonFile;

TEST_CASE("Loading a missing file yields an empty set of values")
{
    TempDirectory temp;

    const JsonFile file = JsonFile::Load(temp.GetPath() / "absent.json");

    CHECK(file.GetCount() == 0);
    CHECK(file.Get("anything").empty());
}

TEST_CASE("A value survives a save and load round trip")
{
    TempDirectory temp;
    const auto path = temp.GetPath() / "settings.json";

    JsonFile written;
    written.Set("lastFolder", "D:\\Repos\\IDE");
    REQUIRE(written.Save(path));

    const JsonFile read = JsonFile::Load(path);

    CHECK(read.GetCount() == 1);
    CHECK(read.Get("lastFolder") == "D:\\Repos\\IDE");
}

TEST_CASE("Writing one key preserves the others")
{
    TempDirectory temp;
    const auto path = temp.GetPath() / "settings.json";

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

    CHECK(file.GetCount() == 1);
    CHECK(file.Get("mode") == "hide");
}

TEST_CASE("An empty value is stored and reads back empty")
{
    TempDirectory temp;
    const auto path = temp.GetPath() / "settings.json";

    JsonFile written;
    written.Set("lastFolder", "");
    REQUIRE(written.Save(path));

    const JsonFile read = JsonFile::Load(path);

    CHECK(read.GetCount() == 1);
    CHECK(read.Get("lastFolder").empty());
}

TEST_CASE("A value containing a newline survives intact")
{
    TempDirectory temp;
    const auto path = temp.GetPath() / "settings.json";

    JsonFile written;
    written.Set("expression", "a\nb");
    REQUIRE(written.Save(path));

    const JsonFile read = JsonFile::Load(path);

    CHECK(read.Get("expression") == "a\nb");
}

TEST_CASE("A value containing an equals sign survives intact")
{
    TempDirectory temp;
    const auto path = temp.GetPath() / "settings.json";

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

    CHECK(read.GetCount() == 0);
    CHECK(read.Get("anything").empty());
}

TEST_CASE("A non-string value reads back as empty rather than throwing")
{
    TempDirectory temp;
    const auto path = temp.MakeFile("settings.json", "{ \"count\": 42, \"mode\": \"dim\" }");

    const JsonFile read = JsonFile::Load(path);

    CHECK(read.Get("count").empty());

    // The neighbouring key proves the file was read at all — without it this test would
    // also pass if parsing had failed outright and everything came back empty.
    CHECK(read.Get("mode") == "dim");
}

TEST_CASE("Saving is human-readable, not a single-line blob")
{
    TempDirectory temp;
    const auto path = temp.GetPath() / "settings.json";

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
    const auto path = temp.GetPath() / "settings.json";

    JsonFile original;
    original.Set("lastFolder", "D:\\Repos\\IDE");
    REQUIRE(original.Save(path));

    JsonFile doomed;
    doomed.Set("lastFolder", "D:\\Elsewhere");
    CHECK_FALSE(doomed.Save(temp.GetPath() / "missing-directory" / "settings.json"));

    // The unrelated file must be untouched, and no stray temporary left behind.
    CHECK(JsonFile::Load(path).Get("lastFolder") == "D:\\Repos\\IDE");
    CHECK_FALSE(std::filesystem::exists(temp.GetPath() / "settings.json.tmp"));
}

TEST_CASE("A value that is not valid UTF-8 is saved instead of throwing")
{
    TempDirectory temp;
    const auto path = temp.GetPath() / "settings.json";

    JsonFile written;

    // Lone continuation bytes: not reachable from today's callers, which pass paths and
    // fixed tokens, but Set takes any std::string and this class is public API of a
    // library meant to be reused. Serialising rejects malformed UTF-8 unless told to
    // substitute, and it would throw out of Core — with the temporary file already open,
    // so the failure would also leave debris behind.
    written.Set("broken", std::string{"aÿþb"});

    REQUIRE(written.Save(path));
    CHECK(std::filesystem::exists(path));
    CHECK_FALSE(std::filesystem::exists(temp.GetPath() / "settings.json.tmp"));

    // Readable afterwards: the mangled characters became replacement characters rather
    // than corrupting the document.
    const JsonFile read = JsonFile::Load(path);
    CHECK_FALSE(read.Get("broken").empty());
}

TEST_CASE("A moved-from file leaves the source usable enough to destroy")
{
    TempDirectory temp;
    const auto path = temp.GetPath() / "settings.json";

    JsonFile original;
    original.Set("lastFolder", "D:\\Repos\\IDE");

    const JsonFile moved = std::move(original);

    // The document lives behind a pointer, so moving empties the source. Nothing may
    // read through it afterwards, but its destructor still runs at the end of this scope
    // and must not fall over.
    CHECK(moved.Get("lastFolder") == "D:\\Repos\\IDE");
    CHECK(moved.Save(path));
}

TEST_CASE("Keys lists what was stored, so a caller can find its own by prefix")
{
    TempDirectory temp;
    const auto path = temp.GetPath() / "settings.json";

    JsonFile file;
    file.Set("key.editor.zoomIn", "Ctrl+U");
    file.Set("wordWrap", "true");
    REQUIRE(file.Save(path));

    const JsonFile loaded = JsonFile::Load(path);
    std::vector<std::string> keys = loaded.GetKeys();
    std::sort(keys.begin(), keys.end());

    REQUIRE(keys.size() == 2);
    CHECK(keys[0] == "key.editor.zoomIn");
    CHECK(keys[1] == "wordWrap");
}

TEST_CASE("Keys on an empty file is empty")
{
    CHECK(JsonFile{}.GetKeys().empty());
}

TEST_CASE("Remove takes the key out rather than leaving it holding an empty value")
{
    TempDirectory temp;
    const auto path = temp.GetPath() / "settings.json";

    JsonFile file;
    file.Set("key.editor.zoomIn", "Ctrl+U");
    file.Set("wordWrap", "true");
    file.Remove("key.editor.zoomIn");

    CHECK(file.GetCount() == 1);
    CHECK(file.Get("key.editor.zoomIn").empty());
    REQUIRE(file.Save(path));

    // Proven through the file too: a key merely blanked would survive the round trip and
    // every reader would have to keep apologising for it.
    const JsonFile read = JsonFile::Load(path);
    CHECK(read.GetKeys() == std::vector<std::string>{"wordWrap"});
}

TEST_CASE("Removing a key that is not there changes nothing")
{
    JsonFile file;
    file.Set("wordWrap", "true");
    file.Remove("absent");

    CHECK(file.GetCount() == 1);
    CHECK(file.Get("wordWrap") == "true");
}
