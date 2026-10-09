#include <doctest/doctest.h>

#include "Settings/AppSettings.h"
#include "Settings/SettingsVersion.h"

#include "Core/Base/JsonFile.h"
#include "Core/FileSystem/FileSystem.h"

#include "TempAppSettings.h"
#include "TempDirectory.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {
	std::string ReadBytes(const std::filesystem::path& path)
	{
		std::ifstream stream{path, std::ios::binary};
		return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
	}

	void WriteText(const std::filesystem::path& path, std::string_view text)
	{
		std::ofstream{path, std::ios::binary} << text;
	}
}

TEST_CASE("AppSettings reads its defaults when there is no file")
{
	const TempDirectory temp;
	const Graphite::AppSettings settings{temp.GetPath() / "graphite.json"};

	CHECK(settings.GetEditorFontSize() == doctest::Approx(0.0f));
	CHECK(settings.IsIndentingWithSpaces());
	CHECK_FALSE(settings.IsWordWrapEnabled());
	CHECK(settings.GetInterfaceScalePercent() == 100);
	CHECK(settings.GetLastFolder().empty());
	CHECK(settings.IsExplorerFollowingActiveFile());
}

TEST_CASE("AppSettings keeps what was set across a flush and a reload")
{
	const TempDirectory temp;
	const std::filesystem::path file = temp.GetPath() / "graphite.json";

	{
		Graphite::AppSettings settings{file};
		settings.SetEditorFontSize(17.0f);
		settings.SetWordWrap(true);
		settings.SetExplorerFollowsActiveFile(false);
		settings.SetColorScheme("Light");
		settings.SetClaudePrice("opus", "1,2,3,4,5");
		settings.Flush();
	}

	const Graphite::AppSettings reloaded{file};

	CHECK(reloaded.GetEditorFontSize() == doctest::Approx(17.0f));
	CHECK(reloaded.IsWordWrapEnabled());
	CHECK_FALSE(reloaded.IsExplorerFollowingActiveFile());
	CHECK(reloaded.GetColorScheme() == "Light");
	REQUIRE(reloaded.GetClaudePrices().size() == 1);
	CHECK(reloaded.GetClaudePrices()[0].first == "opus");
}

TEST_CASE("AppSettings keeps the default shell and the language server path across a reload")
{
	const TempDirectory temp;
	const std::filesystem::path file = temp.GetPath() / "graphite.json";

	{
		const Graphite::AppSettings fresh{file};
		CHECK(fresh.GetDefaultShell().empty());
		CHECK(fresh.GetLanguageServerBinary().empty());
	}

	{
		Graphite::AppSettings settings{file};
		settings.SetDefaultShell("Command Prompt");
		settings.SetLanguageServerBinary(std::filesystem::path{"D:\\tools\\clangd\\clangd.exe"});
		settings.Flush();
	}

	const Graphite::AppSettings reloaded{file};

	CHECK(reloaded.GetDefaultShell() == "Command Prompt");
	CHECK(reloaded.GetLanguageServerBinary() == std::filesystem::path{"D:\\tools\\clangd\\clangd.exe"});
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

// A setter changes the copy in memory and never reads the file. A flush reads it once and lays
// only the keys this process changed over what is there, so a key edited by hand while the
// application runs survives the write, and the flush reads it into memory.
TEST_CASE("AppSettings keeps a hand edit made while it runs, and reads it back on the next flush")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("graphite.json", R"({"wordWrap": "1"})");

	Graphite::AppSettings settings{file};

	temp.MakeFile("graphite.json", R"({"wordWrap": "0", "colorScheme": "Light"})");

	CHECK(settings.IsWordWrapEnabled());
	CHECK(settings.GetColorScheme().empty());

	settings.SetIndentWidth(2);
	settings.Flush();

	const Core::JsonFile written = Core::JsonFile::Load(file);

	CHECK(written.Get("wordWrap") == "0");
	CHECK(written.Get("indentWidth") == "2");
	CHECK(written.Get("colorScheme") == "Light");

	CHECK_FALSE(settings.IsWordWrapEnabled());
	CHECK(settings.GetColorScheme() == "Light");
}

TEST_CASE("AppSettings removes on disk only the key it removed")
{
	TempDirectory temp;
	const std::filesystem::path file =
		temp.MakeFile("graphite.json", R"({"claude.price.opus": "1,2,3,4,5"})");

	Graphite::AppSettings settings{file};

	temp.MakeFile("graphite.json", R"({"claude.price.opus": "1,2,3,4,5", "claude.price.haiku": "1,1,1,1,1"})");

	settings.RemoveClaudePrice("opus");
	settings.Flush();

	const Core::JsonFile written = Core::JsonFile::Load(file);

	CHECK(written.Get("claude.price.opus").empty());
	CHECK(written.Get("claude.price.haiku") == "1,1,1,1,1");
}

TEST_CASE("Two instances on one file keep each other's changes")
{
	const TempDirectory temp;
	const std::filesystem::path file = temp.GetPath() / "graphite.json";

	Graphite::AppSettings first{file};
	Graphite::AppSettings second{file};

	first.SetEditorFontSize(17.0f);
	second.SetWordWrap(true);

	first.Flush();
	second.Flush();

	const Graphite::AppSettings reloaded{file};

	CHECK(reloaded.GetEditorFontSize() == doctest::Approx(17.0f));
	CHECK(reloaded.IsWordWrapEnabled());
}

// A directory where the file should be makes the write fail. The change stays unwritten, so
// the next flush - the one on exit, if nothing else - writes it once the way is clear.
TEST_CASE("AppSettings keeps a change whose write failed and writes it on the next flush")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeDirectory("graphite.json");

	Graphite::AppSettings settings{file};
	settings.SetWordWrap(true);
	settings.Flush();

	std::filesystem::remove(file);
	settings.Flush();

	CHECK(Graphite::AppSettings{file}.IsWordWrapEnabled());
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

	CHECK_FALSE(settings.IsWordWrapEnabled());
	CHECK(settings.IsIndentingWithSpaces());

	settings.Flush();

	CHECK(ReadBytes(file) == corrupt);
}

// A typo in a hand edit made before launch: the file reads as defaults, and the first change
// would write those defaults over every setting the file held. The bytes are set aside first.
TEST_CASE("AppSettings keeps a copy of a corrupt file before its first write goes over it")
{
	TempDirectory temp;
	const std::string corrupt = R"({"wordWrap": "1", "colorScheme": "Light", oops)";
	const std::filesystem::path file = temp.MakeFile("graphite.json", corrupt);
	const std::filesystem::path backup = temp.GetPath() / "graphite.json.bak";

	Graphite::AppSettings settings{file};

	settings.SetIndentWidth(2);
	settings.Flush();

	REQUIRE(std::filesystem::exists(backup));
	CHECK(ReadBytes(backup) == corrupt);
	CHECK(Core::JsonFile::Load(file).Get("indentWidth") == "2");
}

TEST_CASE("AppSettings sets nothing aside when the file it writes over parses")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("graphite.json", R"({"wordWrap": "1"})");

	Graphite::AppSettings settings{file};

	settings.SetIndentWidth(2);
	settings.Flush();

	CHECK_FALSE(std::filesystem::exists(temp.GetPath() / "graphite.json.bak"));
}

TEST_CASE("AppSettings never writes over a file a newer version wrote")
{
	TempDirectory temp;
	const std::string newer = R"({"settingsVersion": ")" +
							  std::to_string(Graphite::kSettingsVersion + 1) +
							  R"(", "wordWrap": "1"})";
	const std::filesystem::path file = temp.MakeFile("graphite.json", newer);

	Graphite::AppSettings settings{file};

	CHECK(settings.IsWordWrapEnabled());

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

// Nothing on disk can be kept from a file that does not parse, so the write falls back to the
// copy in memory, as every write did before flushes merged.
TEST_CASE("AppSettings writes what it holds over a file that stopped parsing while it ran")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("graphite.json", R"({"wordWrap": "1"})");

	Graphite::AppSettings settings{file};

	temp.MakeFile("graphite.json", R"({"wordWrap": "0", oops)");

	settings.SetIndentWidth(2);
	settings.Flush();

	const Core::JsonFile written = Core::JsonFile::Load(file);

	CHECK(written.Get("wordWrap") == "1");
	CHECK(written.Get("indentWidth") == "2");
}

// A file gone or emptied while the application ran - deleted, truncated, or caught halfway
// through another instance's write - has nothing on it to merge onto. Merging onto that
// emptiness would write, and then adopt, only the keys this session changed.
TEST_CASE("AppSettings keeps every setting when the file vanished while it ran")
{
	TempDirectory temp;
	const std::filesystem::path file =
		temp.MakeFile("graphite.json", R"({"wordWrap": "1", "colorScheme": "Light"})");

	Graphite::AppSettings settings{file};

	std::filesystem::remove(file);

	settings.SetIndentWidth(2);
	settings.Flush();

	CHECK(settings.IsWordWrapEnabled());
	CHECK(settings.GetColorScheme() == "Light");

	const Graphite::AppSettings reloaded{file};

	CHECK(reloaded.IsWordWrapEnabled());
	CHECK(reloaded.GetColorScheme() == "Light");
	CHECK(reloaded.GetIndentWidth() == 2);
}

TEST_CASE("AppSettings keeps every setting when the file was emptied while it ran")
{
	TempDirectory temp;
	const std::filesystem::path file =
		temp.MakeFile("graphite.json", R"({"wordWrap": "1", "colorScheme": "Light"})");

	Graphite::AppSettings settings{file};

	temp.MakeFile("graphite.json", "");

	settings.SetIndentWidth(2);
	settings.Flush();

	CHECK(settings.IsWordWrapEnabled());
	CHECK(settings.GetColorScheme() == "Light");

	const Graphite::AppSettings reloaded{file};

	CHECK(reloaded.IsWordWrapEnabled());
	CHECK(reloaded.GetColorScheme() == "Light");
	CHECK(reloaded.GetIndentWidth() == 2);
}

TEST_CASE("AppSettings reads a language's server choice and a server's settings from nested values")
{
	const TempDirectory temp;
	const std::filesystem::path file = temp.GetPath() / "graphite.json";

	WriteText(file, R"({
		"languages": { "Cpp": { "language_server": "clangd" } },
		"lsp": { "clangd": {
			"binary": { "path": "D:\\Tools\\clangd.exe", "arguments": ["--log=verbose", 7] },
			"initialization_options": { "fallbackFlags": ["-std=c++20"] },
			"settings": { "compilationDatabasePath": "build" } } }
	})");

	const Graphite::AppSettings settings{file};

	CHECK(settings.GetLanguageServerChoice("Cpp") == std::optional<std::string>{"clangd"});
	CHECK_FALSE(settings.GetLanguageServerChoice("CSharp").has_value());

	const Graphite::LanguageServerSettings clangd = settings.GetLanguageServerSettings("clangd");

	CHECK(clangd.BinaryPath == std::filesystem::path{L"D:\\Tools\\clangd.exe"});
	CHECK(clangd.Arguments == std::vector<std::string>{"--log=verbose"});
	CHECK(clangd.InitializationOptionsJson == R"({"fallbackFlags":["-std=c++20"]})");
	CHECK(clangd.SettingsJson == R"({"compilationDatabasePath":"build"})");
}

TEST_CASE("AppSettings reads a server with no entry as nothing set")
{
	TempAppSettings settings;

	const Graphite::LanguageServerSettings roslyn = settings.Get().GetLanguageServerSettings("roslyn");

	CHECK(roslyn.BinaryPath.empty());
	CHECK(roslyn.Arguments.empty());
	CHECK(roslyn.InitializationOptionsJson.empty());
	CHECK(roslyn.SettingsJson.empty());
}

TEST_CASE("AppSettings reads hand-written language entries of the wrong shape as nothing set and leaves them on disk")
{
	const TempDirectory temp;
	const std::filesystem::path file = temp.GetPath() / "graphite.json";

	WriteText(file, R"({"languages": "Cpp", "lsp": {"clangd": "fast", "roslyn": {"binary": 3}}})");

	Graphite::AppSettings settings{file};

	CHECK_FALSE(settings.GetLanguageServerChoice("Cpp").has_value());
	CHECK(settings.GetLanguageServerSettings("clangd").BinaryPath.empty());
	CHECK(settings.GetLanguageServerSettings("roslyn").BinaryPath.empty());

	settings.SetEditorFontSize(15.0f);
	settings.Flush();

	const std::string written = ReadBytes(file);

	CHECK(written.find(R"("fast")") != std::string::npos);
	CHECK(written.find(R"("binary": 3)") != std::string::npos);
}

TEST_CASE("AppSettings keeps a server path outside the ANSI code page")
{
	const TempDirectory temp;
	const std::filesystem::path file = temp.GetPath() / "graphite.json";
	const std::filesystem::path server = std::filesystem::path{L"D:\\Narz\u0119dzia\\\u4E2D\u6587"} / L"clangd.exe";

	const nlohmann::json content{{"lsp", {{"clangd", {{"binary", {{"path", Core::ToUtf8(server)}}}}}}}};
	WriteText(file, content.dump());

	const Graphite::AppSettings settings{file};

	CHECK(settings.GetLanguageServerSettings("clangd").BinaryPath == server);
}

TEST_CASE("Two instances on one file keep each other's nested changes")
{
	const TempDirectory temp;
	const std::filesystem::path file = temp.GetPath() / "graphite.json";

	Graphite::AppSettings first{file};
	Graphite::AppSettings second{file};

	first.SetLanguageServerChoice("Cpp", "clangd");
	second.SetLanguageServerChoice("CSharp", "roslyn");

	first.Flush();
	second.Flush();

	const Graphite::AppSettings reloaded{file};

	CHECK(reloaded.GetLanguageServerChoice("Cpp") == std::optional<std::string>{"clangd"});
	CHECK(reloaded.GetLanguageServerChoice("CSharp") == std::optional<std::string>{"roslyn"});
}

TEST_CASE("AppSettings keeps a nested value edited by hand while it runs")
{
	const TempDirectory temp;
	const std::filesystem::path file = temp.GetPath() / "graphite.json";

	Graphite::AppSettings settings{file};
	settings.SetEditorFontSize(15.0f);

	WriteText(file, R"({"lsp":{"clangd":{"binary":{"path":"C:\\clangd.exe"}}}})");

	settings.Flush();

	const Graphite::AppSettings reloaded{file};

	CHECK(reloaded.GetLanguageServerSettings("clangd").BinaryPath == std::filesystem::path{L"C:\\clangd.exe"});
	CHECK(reloaded.GetEditorFontSize() == doctest::Approx(15.0f));
}