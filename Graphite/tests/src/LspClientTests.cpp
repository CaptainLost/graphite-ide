#include <doctest/doctest.h>

#include "Core/Fs/FileSystem.h"
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
	CHECK(declarations[0].Utf16Column == 2);
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

TEST_CASE("A path with a non-ASCII character round-trips through its escaped bytes")
{
	// The case the encoding exists for: "ą" is two UTF-8 bytes and neither is spellable in a URI,
	// so both are escaped, and a server that compares URIs as strings only agrees with us if the
	// way back produces exactly the path we started from.
	const std::filesystem::path path = Core::FromUtf8("D:/\xC4\x85\x62/main.cpp");

	const std::string uri = Graphite::ToFileUri(path);

	CHECK(uri == "file:///D:/%C4%85b/main.cpp");
	CHECK(Graphite::FromFileUri(uri) == path);
}

namespace {

	// A client that did everything except start a process: the handshake has gone out into a
	// queue nothing drains, and Deliver feeds it the bodies a server would have sent. This is how
	// a machine with no clangd still exercises the handshake, the queue and the answers to a
	// server's own requests.
	Graphite::LspClient Offline(const std::filesystem::path& root)
	{
		return Graphite::LspClient{*Graphite::ServerConfigFor(Graphite::LanguageId::Cpp), root,
								   Graphite::LspClient::WithoutServer{}};
	}

	bool AnySent(const std::vector<std::string>& sent, std::string_view needle)
	{
		for (const std::string& message : sent)
		{
			if (message.find(needle) != std::string::npos)
			{
				return true;
			}
		}

		return false;
	}

	Graphite::ServerStatus StatusOf(Graphite::LspClient& client)
	{
		std::vector<Graphite::LspClient::Answer> answers;
		Graphite::ServerStatus status;

		client.Drain(answers, status);

		return status;
	}

	// The id BeginHandshake allocates, since it is the first request a client ever makes.
	constexpr Graphite::RequestId kInitializeId = 1;
}

TEST_CASE("The handshake goes out first and carries the capabilities the server reports through")
{
	TempDirectory temp;
	Graphite::LspClient client = Offline(temp.GetPath());

	const std::vector<std::string> sent = client.TakeSent();

	REQUIRE(sent.size() == 1);
	CHECK(sent[0].find("Content-Length: ") == 0);
	CHECK(sent[0].find(R"("method":"initialize")") != std::string::npos);

	// Without this the server never creates a progress token, and the status bar has no
	// percentage to report at all.
	CHECK(sent[0].find(R"("workDoneProgress":true)") != std::string::npos);
}

TEST_CASE("An initialize reply that is an error leaves the client failed, not ready")
{
	TempDirectory temp;
	temp.MakeFile("compile_flags.txt", "-std=c++20\n");

	Graphite::LspClient client = Offline(temp.GetPath());
	client.TakeSent();

	client.Deliver(R"({"jsonrpc":"2.0","id":1,"error":{"code":-32603,"message":"no such flag"}})");

	const Graphite::ServerStatus status = StatusOf(client);

	CHECK(status.State == Graphite::ServerState::Failed);
	CHECK(status.Detail == "no such flag");
}

TEST_CASE("A successful initialize reply reports the server's version and releases the queue")
{
	TempDirectory temp;
	temp.MakeFile("compile_flags.txt", "-std=c++20\n");

	Graphite::LspClient client = Offline(temp.GetPath());
	client.TakeSent();

	// Asked before the handshake finished: a server is entitled to fail anything that arrives
	// ahead of the initialize reply, so this has to wait its turn.
	client.OpenDocument(temp.GetPath() / "main.cpp", "int Main() { return 0; }\n");

	CHECK(client.TakeSent().empty());

	client.Deliver(
		R"({"jsonrpc":"2.0","id":1,"result":{"serverInfo":{"name":"clangd","version":"18.1.3"}}})");

	const std::vector<std::string> sent = client.TakeSent();

	REQUIRE(sent.size() == 2);
	CHECK(sent[0].find(R"("method":"initialized")") != std::string::npos);
	CHECK(sent[1].find(R"("method":"textDocument/didOpen")") != std::string::npos);

	const Graphite::ServerStatus status = StatusOf(client);

	CHECK(status.State == Graphite::ServerState::Ready);
	CHECK(status.Detail == "clangd 18.1.3");
}

TEST_CASE("A root with no compile database settles into saying so rather than into Ready")
{
	TempDirectory temp;
	Graphite::LspClient client = Offline(temp.GetPath());

	client.Deliver(R"({"jsonrpc":"2.0","id":1,"result":{}})");

	CHECK(StatusOf(client).State == Graphite::ServerState::NoCompileDatabase);
}

TEST_CASE("A progress notification moves the percentage the status bar shows")
{
	TempDirectory temp;
	temp.MakeFile("compile_flags.txt", "-std=c++20\n");

	Graphite::LspClient client = Offline(temp.GetPath());
	client.Deliver(R"({"jsonrpc":"2.0","id":1,"result":{}})");

	client.Deliver(
		R"({"method":"$/progress","params":{"token":"i","value":{"kind":"begin","title":"indexing"}}})");

	CHECK(StatusOf(client).State == Graphite::ServerState::Indexing);

	client.Deliver(
		R"({"method":"$/progress","params":{"token":"i","value":{"kind":"report","percentage":40}}})");

	CHECK(StatusOf(client).Percent == 40);

	client.Deliver(R"({"method":"$/progress","params":{"token":"i","value":{"kind":"end"}}})");

	const Graphite::ServerStatus status = StatusOf(client);

	CHECK(status.State == Graphite::ServerState::Ready);
	CHECK(status.Percent == -1);
}

TEST_CASE("A configuration request is answered with one empty settings object per item")
{
	TempDirectory temp;
	Graphite::LspClient client = Offline(temp.GetPath());
	client.TakeSent();

	client.Deliver(
		R"({"id":7,"method":"workspace/configuration","params":{"items":[{"section":"clangd"}]}})");

	const std::vector<std::string> sent = client.TakeSent();

	REQUIRE(sent.size() == 1);
	CHECK(sent[0].find(R"("id":7)") != std::string::npos);
	CHECK(sent[0].find(R"("result":[{}])") != std::string::npos);
}

TEST_CASE("A progress-token request is answered with null so the notifications start")
{
	TempDirectory temp;
	Graphite::LspClient client = Offline(temp.GetPath());
	client.TakeSent();

	client.Deliver(R"({"id":8,"method":"window/workDoneProgress/create","params":{"token":"i"}})");

	const std::vector<std::string> sent = client.TakeSent();

	REQUIRE(sent.size() == 1);
	CHECK(sent[0].find(R"("id":8)") != std::string::npos);
	CHECK(sent[0].find(R"("result":null)") != std::string::npos);
}

TEST_CASE("A server request nothing here understands is refused rather than left waiting")
{
	TempDirectory temp;
	Graphite::LspClient client = Offline(temp.GetPath());
	client.TakeSent();

	client.Deliver(R"({"id":9,"method":"workspace/applyEdit","params":{}})");

	const std::vector<std::string> sent = client.TakeSent();

	REQUIRE(sent.size() == 1);
	CHECK(sent[0].find(R"("id":9)") != std::string::npos);
	CHECK(sent[0].find(R"("code":-32601)") != std::string::npos);
}

TEST_CASE("An answer is correlated to the question that was asked under its id")
{
	TempDirectory temp;
	temp.MakeFile("compile_flags.txt", "-std=c++20\n");

	Graphite::LspClient client = Offline(temp.GetPath());
	client.Deliver(R"({"jsonrpc":"2.0","id":1,"result":{}})");
	client.TakeSent();

	const std::filesystem::path source = temp.GetPath() / "main.cpp";

	client.OpenDocument(source, "int Main() { return 0; }\n");

	const Graphite::RequestId definition = client.AskDefinition({source, 1, 21});
	const Graphite::RequestId symbols = client.AskSymbols(source);

	CHECK(definition != kInitializeId);
	CHECK(AnySent(client.TakeSent(), R"("method":"textDocument/documentSymbol")"));

	// Answered out of order, and each read as the question it was: a symbol reply parsed as
	// locations, or the other way round, would silently yield nothing at all.
	client.Deliver(R"({"id":)" + std::to_string(symbols) +
				   R"(,"result":[{"name":"Main","kind":12,"range":{"start":{"line":0,"character":4}}}]})");

	client.Deliver(R"({"id":)" + std::to_string(definition) +
				   R"(,"result":[{"uri":"file:///D:/a.cpp","range":{"start":{"line":3,"character":0}}}]})");

	std::vector<Graphite::LspClient::Answer> answers;
	Graphite::ServerStatus status;
	client.Drain(answers, status);

	REQUIRE(answers.size() == 2);

	CHECK(answers[0].Id == symbols);
	REQUIRE(answers[0].Declarations.size() == 1);
	CHECK(answers[0].Declarations[0].Name == "Main");

	CHECK(answers[1].Id == definition);
	REQUIRE(answers[1].Locations.size() == 1);
	CHECK(answers[1].Locations[0].Line == 3);
}

TEST_CASE("A cancelled question's reply is dropped rather than delivered to nobody")
{
	TempDirectory temp;
	Graphite::LspClient client = Offline(temp.GetPath());
	client.Deliver(R"({"jsonrpc":"2.0","id":1,"result":{}})");

	const std::filesystem::path source = temp.GetPath() / "main.cpp";
	client.OpenDocument(source, "int Main() { return 0; }\n");

	const Graphite::RequestId id = client.AskReferences({source, 0, 4});
	client.Cancel(id);

	client.Deliver(R"({"id":)" + std::to_string(id) +
				   R"(,"result":[{"uri":"file:///D:/a.cpp","range":{"start":{"line":0,"character":0}}}]})");

	std::vector<Graphite::LspClient::Answer> answers;
	Graphite::ServerStatus status;
	client.Drain(answers, status);

	CHECK(answers.empty());
}

TEST_CASE("A document's version increases per document, as the protocol requires")
{
	TempDirectory temp;
	Graphite::LspClient client = Offline(temp.GetPath());
	client.Deliver(R"({"jsonrpc":"2.0","id":1,"result":{}})");
	client.TakeSent();

	const std::filesystem::path first = temp.GetPath() / "a.cpp";
	const std::filesystem::path second = temp.GetPath() / "b.cpp";

	client.OpenDocument(first, "int A();\n");
	client.OpenDocument(second, "int B();\n");
	client.ChangeDocument(first, "int A(int);\n");
	client.ChangeDocument(first, "int A(int, int);\n");

	const std::vector<std::string> sent = client.TakeSent();

	REQUIRE(sent.size() == 4);
	CHECK(sent[2].find(R"("version":2)") != std::string::npos);
	CHECK(sent[3].find(R"("version":3)") != std::string::npos);

	// The second document's own count is untouched by the first's edits.
	client.ChangeDocument(second, "int B(int);\n");

	const std::vector<std::string> later = client.TakeSent();

	REQUIRE(later.size() == 1);
	CHECK(later[0].find(R"("version":2)") != std::string::npos);

	// Full text every time: a range this side computed wrongly would corrupt the server's copy
	// silently, and nothing here could notice.
	CHECK(later[0].find(R"("contentChanges":[{"text":"int B(int);\n"}])") != std::string::npos);
}

TEST_CASE("A change to a document the server was never told about sends nothing")
{
	TempDirectory temp;
	Graphite::LspClient client = Offline(temp.GetPath());
	client.Deliver(R"({"jsonrpc":"2.0","id":1,"result":{}})");
	client.TakeSent();

	client.ChangeDocument(temp.GetPath() / "never-opened.cpp", "int A();\n");
	client.SaveDocument(temp.GetPath() / "never-opened.cpp");

	CHECK(client.TakeSent().empty());
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
