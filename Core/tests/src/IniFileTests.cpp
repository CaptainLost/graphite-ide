#include <doctest/doctest.h>

#include "Core/Base/IniFile.h"

#include "TempDirectory.h"

using core::IniFile;

TEST_CASE("Loading a missing file yields an empty set of values")
{
    TempDirectory temp;

    const IniFile file = IniFile::Load(temp.Path() / "absent.ini");

    CHECK(file.Count() == 0);
    CHECK(file.Get("anything").empty());
}

TEST_CASE("A value survives a save and load round trip")
{
    TempDirectory temp;
    const auto path = temp.Path() / "settings.ini";

    IniFile written;
    written.Set("lastFolder", "D:\\Repos\\IDE");
    REQUIRE(written.Save(path));

    const IniFile read = IniFile::Load(path);

    CHECK(read.Count() == 1);
    CHECK(read.Get("lastFolder") == "D:\\Repos\\IDE");
}

TEST_CASE("Writing one key preserves the others")
{
    TempDirectory temp;
    const auto path = temp.Path() / "settings.ini";

    IniFile first;
    first.Set("lastFolder", "D:\\Repos\\IDE");
    first.Set("ignoredDisplay", "dim");
    REQUIRE(first.Save(path));

    IniFile second = IniFile::Load(path);
    second.Set("ignoredDisplay", "hide");
    REQUIRE(second.Save(path));

    const IniFile read = IniFile::Load(path);

    CHECK(read.Get("lastFolder") == "D:\\Repos\\IDE");
    CHECK(read.Get("ignoredDisplay") == "hide");
}

TEST_CASE("Setting an existing key replaces it rather than duplicating it")
{
    IniFile file;
    file.Set("mode", "dim");
    file.Set("mode", "hide");

    CHECK(file.Count() == 1);
    CHECK(file.Get("mode") == "hide");
}

TEST_CASE("A value containing an equals sign survives intact")
{
    TempDirectory temp;
    const auto path = temp.Path() / "settings.ini";

    IniFile written;
    written.Set("expression", "a=b=c");
    REQUIRE(written.Save(path));

    CHECK(IniFile::Load(path).Get("expression") == "a=b=c");
}

TEST_CASE("An empty value is stored and reads back empty")
{
    TempDirectory temp;
    const auto path = temp.Path() / "settings.ini";

    IniFile written;
    written.Set("lastFolder", "");
    REQUIRE(written.Save(path));

    const IniFile read = IniFile::Load(path);

    CHECK(read.Count() == 1);
    CHECK(read.Get("lastFolder").empty());
}

TEST_CASE("Blank lines and lines without a separator are skipped")
{
    TempDirectory temp;
    const auto path = temp.MakeFile("settings.ini", "\nnonsense\nmode=dim\n\n");

    const IniFile read = IniFile::Load(path);

    CHECK(read.Count() == 1);
    CHECK(read.Get("mode") == "dim");
}

TEST_CASE("A trailing carriage return is not part of the value")
{
    TempDirectory temp;
    const auto path = temp.MakeFile("settings.ini", "mode=dim\r\n");

    // The file is opened in text mode, but a file written on another machine can still
    // carry a stray CR; it must not end up inside the value.
    CHECK(IniFile::Load(path).Get("mode") == "dim");
}

TEST_CASE("A failed save leaves the previous file intact")
{
    TempDirectory temp;
    const auto path = temp.Path() / "settings.ini";

    IniFile original;
    original.Set("lastFolder", "D:\\Repos\\IDE");
    REQUIRE(original.Save(path));

    IniFile doomed;
    doomed.Set("lastFolder", "D:\\Elsewhere");
    CHECK_FALSE(doomed.Save(temp.Path() / "missing-directory" / "settings.ini"));

    // The unrelated file must be untouched, and no stray temporary left behind.
    CHECK(IniFile::Load(path).Get("lastFolder") == "D:\\Repos\\IDE");
    CHECK_FALSE(std::filesystem::exists(temp.Path() / "settings.ini.tmp"));
}
