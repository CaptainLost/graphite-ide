#include <doctest/doctest.h>

#include "Settings/AppSettings.h"
#include "Settings/SettingsVersion.h"

#include "Core/Base/JsonFile.h"

#include "TempDirectory.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {
	std::string ReadBytes(const std::filesystem::path& path)
	{
		std::ifstream stream{path, std::ios::binary};
		return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
	}
}

TEST_CASE("AppSettings reads its defaults when there is no file")
{
	const TempDirectory temp;
	const Graphite::AppSettings settings{temp.GetPath() / "graphite.json"};

	CHECK(settings.GetEditorFontSize() == doctest::Approx(0.0f));
	CHECK(settings.GetIndentWithSpaces());
	CHECK_FALSE(settings.GetWordWrap());
	CHECK(settings.GetInterfaceScalePercent() == 100);
	CHECK(settings.GetLastFolder().empty());
}

TEST_CASE("AppSettings keeps what was set across a flush and a reload")
{
	const TempDirectory temp;
	const std::filesystem::path file = temp.GetPath() / "graphite.json";

	{
		Graphite::AppSettings settings{file};
		settings.SetEditorFontSize(17.0f);
		settings.SetWordWrap(true);
		settings.SetColorScheme("Light");
		settings.SetClaudePrice("opus", "1,2,3,4,5");
		settings.Flush();
	}

	const Graphite::AppSettings reloaded{file};

	CHECK(reloaded.GetEditorFontSize() == doctest::Approx(17.0f));
	CHECK(reloaded.GetWordWrap());
	CHECK(reloaded.GetColorScheme() == "Light");
	REQUIRE(reloaded.GetClaudePrices().size() == 1);
	CHECK(reloaded.GetClaudePrices()[0].first == "opus");
}

TEST_CASE("AppSettings writes nothing until it is flushed, and nothing when nothing changed")
{
	const TempDirectory temp;
	const std::filesystem::path file = temp.GetPath() / "graphite.json";

	Graphite::AppSettings settings{file};
	settings.Flush();

	CHECK_FALSE(std::filesystem::exists(file));

	settings.SetWordWrap(true);

	CHECK_FALSE(std::filesystem::exists(file));

	settings.Flush();

	CHECK(std::filesystem::exists(file));
}

// A setter changes the copy in memory and a flush writes that copy: neither goes back to the
// file. So a key added to the file behind the application's back while it runs is gone after
// the next write — the price of not re-reading the file per value.
TEST_CASE("AppSettings neither reads nor merges the file again once loaded")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("graphite.json", R"({"wordWrap": "1"})");

	Graphite::AppSettings settings{file};

	temp.MakeFile("graphite.json", R"({"wordWrap": "0", "colorScheme": "Light"})");

	CHECK(settings.GetWordWrap());
	CHECK(settings.GetColorScheme().empty());

	settings.SetIndentWidth(2);
	settings.Flush();

	const Core::JsonFile written = Core::JsonFile::Load(file);

	CHECK(written.Get("wordWrap") == "1");
	CHECK(written.Get("indentWidth") == "2");
	CHECK(written.Get("colorScheme").empty());
}

// The rule JsonFile::Load has always had: a file that does not parse reads as an empty one.
// Loading it, reading it and flushing with nothing changed must leave its bytes alone, so a
// hand-edit with a typo in it survives a launch that changes no setting.
TEST_CASE("AppSettings reads a corrupt file as defaults and leaves it as it was")
{
	TempDirectory temp;
	const std::string corrupt = R"({"wordWrap": "1", oops)";
	const std::filesystem::path file = temp.MakeFile("graphite.json", corrupt);

	Graphite::AppSettings settings{file};

	CHECK_FALSE(settings.GetWordWrap());
	CHECK(settings.GetIndentWithSpaces());

	settings.Flush();

	CHECK(ReadBytes(file) == corrupt);
}

TEST_CASE("AppSettings never writes over a file a newer version wrote")
{
	TempDirectory temp;
	const std::string newer = R"({"settingsVersion": ")" +
							  std::to_string(Graphite::kSettingsVersion + 1) +
							  R"(", "wordWrap": "1"})";
	const std::filesystem::path file = temp.MakeFile("graphite.json", newer);

	Graphite::AppSettings settings{file};

	CHECK(settings.GetWordWrap());

	settings.SetWordWrap(false);
	settings.Flush();

	CHECK(ReadBytes(file) == newer);
}

TEST_CASE("AppSettings stamps the version it writes")
{
	const TempDirectory temp;
	const std::filesystem::path file = temp.GetPath() / "graphite.json";

	Graphite::AppSettings settings{file};
	settings.SetWordWrap(true);
	settings.Flush();

	CHECK(Core::JsonFile::Load(file).Get("settingsVersion") ==
		  std::to_string(Graphite::kSettingsVersion));
}

TEST_CASE("AppSettings forgets a removed Claude price on the next write")
{
	const TempDirectory temp;
	const std::filesystem::path file = temp.GetPath() / "graphite.json";

	{
		Graphite::AppSettings settings{file};
		settings.SetClaudePrice("opus", "1,2,3,4,5");
		settings.SetClaudePrice("haiku", "1,1,1,1,1");
		settings.Flush();
		settings.RemoveClaudePrice("opus");
		settings.Flush();
	}

	const Graphite::AppSettings reloaded{file};

	REQUIRE(reloaded.GetClaudePrices().size() == 1);
	CHECK(reloaded.GetClaudePrices()[0].first == "haiku");
}
