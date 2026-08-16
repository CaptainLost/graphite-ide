#include <doctest/doctest.h>

#include "Lang/Server/LspClient.h"
#include "Lang/Server/ServerDiscovery.h"
#include "Lang/Server/ServerRegistry.h"
#include "TempDirectory.h"

#include <chrono>
#include <fstream>
#include <thread>

TEST_CASE("A path becomes a file URI a server accepts")
{
	CHECK(Graphite::ToFileUri("D:\\Repos\\IDE\\main.cpp") == "file:///D:/Repos/IDE/main.cpp");
	CHECK(Graphite::ToFileUri("D:\\a b\\main.cpp") == "file:///D:/a%20b/main.cpp");
}

TEST_CASE("A file URI becomes the path it named")
{
	CHECK(Graphite::FromFileUri("file:///D:/Repos/IDE/main.cpp") ==
		  std::filesystem::path("D:/Repos/IDE/main.cpp"));
	CHECK(Graphite::FromFileUri("file:///D:/a%20b/main.cpp") ==
		  std::filesystem::path("D:/a b/main.cpp"));
}

TEST_CASE("A byte column becomes the UTF-16 column a server counts in")
{
	// "ąb" is two bytes then one; a server counts the first character as one UTF-16 unit.
	CHECK(Graphite::ToUtf16Column("\xC4\x85""b", 3) == 2);
	CHECK(Graphite::ToUtf16Column("plain", 3) == 3);

	// U+1F600 is four bytes and two UTF-16 units.
	CHECK(Graphite::ToUtf16Column("\xF0\x9F\x98\x80""x", 5) == 3);
}

TEST_CASE("A UTF-16 column becomes the byte column this editor counts in")
{
	CHECK(Graphite::FromUtf16Column("\xC4\x85""b", 2) == 3);
	CHECK(Graphite::FromUtf16Column("plain", 3) == 3);
	CHECK(Graphite::FromUtf16Column("\xF0\x9F\x98\x80""x", 3) == 5);
}

TEST_CASE("A definition reply parses into locations")
{
	const auto locations = Graphite::ParseLocations(nlohmann::json::parse(
		R"([{"uri":"file:///D:/a.cpp","range":{"start":{"line":9,"character":4}}}])"));

	REQUIRE(locations.size() == 1);
	CHECK(locations[0].Path == std::filesystem::path("D:/a.cpp"));
	CHECK(locations[0].Line == 9);
}

TEST_CASE("A single-location reply that is not an array parses too")
{
	const auto locations = Graphite::ParseLocations(nlohmann::json::parse(
		R"({"uri":"file:///D:/a.cpp","range":{"start":{"line":1,"character":0}}})"));

	CHECK(locations.size() == 1);
}

TEST_CASE("A documentSymbol reply keeps functions and types and flattens children")
{
	const auto declarations = Graphite::ParseSymbols(nlohmann::json::parse(
		R"([{"name":"Renderer","kind":5,"range":{"start":{"line":2,"character":0}},
			 "children":[{"name":"Draw","kind":6,
						  "range":{"start":{"line":4,"character":1}}}]}])"));

	REQUIRE(declarations.size() == 2);
	CHECK(declarations[0].Name == "Renderer");
	CHECK(declarations[0].Kind == Graphite::DeclarationKind::Type);
	CHECK(declarations[1].Name == "Draw");
	CHECK(declarations[1].Kind == Graphite::DeclarationKind::Function);
	CHECK(declarations[1].Line == 4);
}

TEST_CASE("A reply that is null yields no locations rather than failing")
{
	CHECK(Graphite::ParseLocations(nlohmann::json()).empty());
}

TEST_CASE("A symbol reply that names nothing this editor shows yields nothing")
{
	// SymbolKind 13 is Variable: no lens belongs over one, so it is skipped rather than
	// stored as DeclarationKind::Other and filtered by whoever renders it later.
	const auto declarations = Graphite::ParseSymbols(nlohmann::json::parse(
		R"([{"name":"count","kind":13,"range":{"start":{"line":0,"character":0}}}])"));

	CHECK(declarations.empty());
}

TEST_CASE("A location reply missing the fields it should carry yields nothing rather than failing")
{
	CHECK(Graphite::ParseLocations(nlohmann::json::parse(R"([{"range":{}}])")).empty());
	CHECK(Graphite::ParseLocations(nlohmann::json::parse(R"("a string")")).empty());
	CHECK(Graphite::ParseSymbols(nlohmann::json::parse(R"([{"kind":5}])")).empty());
	CHECK(Graphite::ParseSymbols(nlohmann::json()).empty());
}

TEST_CASE("A workspace symbol shape without children still parses through its location")
{
	// workspace/symbol and older documentSymbol servers answer with SymbolInformation, whose
	// position sits under "location"/"range" rather than directly under "range".
	const auto declarations = Graphite::ParseSymbols(nlohmann::json::parse(
		R"([{"name":"Draw","kind":12,
			 "location":{"uri":"file:///D:/a.cpp","range":{"start":{"line":7,"character":2}}}}])"));

	REQUIRE(declarations.size() == 1);
	CHECK(declarations[0].Line == 7);
	CHECK(declarations[0].Column == 2);
}

TEST_CASE("A column conversion past the end of its line stops at the end")
{
	CHECK(Graphite::ToUtf16Column("ab", 99) == 2);
	CHECK(Graphite::FromUtf16Column("ab", 99) == 2);
	CHECK(Graphite::ToUtf16Column("", 0) == 0);
	CHECK(Graphite::FromUtf16Column("", 0) == 0);
}

TEST_CASE("A URI round-trips through the encoding a server compares as a string")
{
	const std::filesystem::path path = "D:/a b/#hash/plus+/main.cpp";

	CHECK(Graphite::FromFileUri(Graphite::ToFileUri(path)) == path);
	CHECK(Graphite::ToFileUri(path).find(' ') == std::string::npos);
}

TEST_CASE("A question asked of a server that never started is answered rather than left hanging")
{
	TempDirectory temp;

	Graphite::LspClient client{*Graphite::ServerConfigFor(Graphite::LanguageId::Cpp),
							   "no-such-language-server-4c1e.exe", temp.GetPath()};

	const Graphite::RequestId id = client.AskDefinition({temp.GetPath() / "main.cpp", 0, 0});

	std::vector<Graphite::LspClient::Answer> answers;
	Graphite::ServerStatus status;
	client.Drain(answers, status);

	CHECK(status.State == Graphite::ServerState::Failed);
	CHECK(status.Name == "clangd");

	REQUIRE(answers.size() == 1);
	CHECK(answers[0].Id == id);
	CHECK(answers[0].Failed);
}

TEST_CASE("A client whose child never speaks the protocol still shuts down")
{
	// cmd.exe is not a language server: it will answer nothing, and it may sit waiting on its
	// input forever. The destructor still has to terminate it and join the reader thread — a
	// hang here, not an assertion, is what failure looks like.
	TempDirectory temp;

	Graphite::LspClient client{*Graphite::ServerConfigFor(Graphite::LanguageId::Cpp), "cmd.exe",
							   temp.GetPath()};

	client.OpenDocument(temp.GetPath() / "main.cpp", "int Main() { return 0; }\n");

	std::vector<Graphite::LspClient::Answer> answers;
	Graphite::ServerStatus status;
	client.Drain(answers, status);

	CHECK(answers.empty());
}

// Skipped by default: a machine with no clangd would otherwise get a red suite for something
// it cannot fix. Run with --no-skip to exercise the real handshake.
TEST_CASE("clangd answers a definition request" * doctest::skip(true))
{
	const auto binary = Graphite::FindServerBinary("clangd", {});
	REQUIRE(binary);

	TempDirectory temp;
	const std::filesystem::path source = temp.GetPath() / "main.cpp";

	std::ofstream{temp.GetPath() / "compile_flags.txt"} << "-std=c++20\n";
	std::ofstream{source} << "int Answer() { return 42; }\nint Main() { return Answer(); }\n";

	Graphite::LspClient client{*Graphite::ServerConfigFor(Graphite::LanguageId::Cpp),
							   binary.GetValue(), temp.GetPath()};

	const std::string text = "int Answer() { return 42; }\nint Main() { return Answer(); }\n";
	client.OpenDocument(source, text);

	// Line 1, column 21 is inside the call to Answer.
	const Graphite::RequestId id = client.AskDefinition({source, 1, 21});

	std::vector<Graphite::LspClient::Answer> answers;
	Graphite::ServerStatus status;

	for (int attempt = 0; attempt < 200 && answers.empty(); ++attempt)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		client.Drain(answers, status);
	}

	REQUIRE(answers.size() == 1);
	CHECK(answers[0].Id == id);
	REQUIRE(answers[0].Locations.size() == 1);
	CHECK(answers[0].Locations[0].Line == 0);
}
