#include <doctest/doctest.h>

#include "Core/Fs/FileSystem.h"
#include "Lang/Server/LspClient.h"
#include "Lang/Server/ServerDiscovery.h"
#include "Lang/Server/ServerRegistry.h"
#include "TempDirectory.h"

#include <chrono>
#include <fstream>
#include <thread>
#include <type_traits>

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
	CHECK(Graphite::ToUtf16Column("\xC4\x85""b", 3) == Graphite::Utf16Column{2});
	CHECK(Graphite::ToUtf16Column("plain", 3) == Graphite::Utf16Column{3});

	// U+1F600 is four bytes and two UTF-16 units.
	CHECK(Graphite::ToUtf16Column("\xF0\x9F\x98\x80""x", 5) == Graphite::Utf16Column{3});
}

TEST_CASE("A column is a type, so a byte count cannot be passed as one by accident")
{
	// The guarantee the type exists for, since nothing at runtime can catch a missed conversion:
	// a plain number is not a Utf16Column, and brace elision inside FilePosition does not make it
	// one. Both of these are compile errors, which is the whole mechanism:
	//
	//     client.AskDefinition({source, 1, 21});
	//     Graphite::Utf16Column column = 21;
	//
	CHECK_FALSE(std::is_convertible_v<std::size_t, Graphite::Utf16Column>);
	CHECK(std::is_constructible_v<Graphite::Utf16Column, std::size_t>);

	// And it still compares and defaults like the number it wraps.
	CHECK(Graphite::Utf16Column{} == Graphite::Utf16Column{0});
	CHECK(Graphite::Utf16Column{7}.Value == 7);
}

TEST_CASE("A UTF-16 column becomes the byte column this editor counts in")
{
	CHECK(Graphite::FromUtf16Column("\xC4\x85""b", Graphite::Utf16Column{2}) == 3);
	CHECK(Graphite::FromUtf16Column("plain", Graphite::Utf16Column{3}) == 3);
	CHECK(Graphite::FromUtf16Column("\xF0\x9F\x98\x80""x", Graphite::Utf16Column{3}) == 5);
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

// The numbers a semantic token reply carries mean nothing on their own: which index is a type
// and which a macro is declared by the server, in the legend it sends back from initialize.
// Hard-coding the order would work against clangd and break against the next server added to the
// table, which is exactly the coupling ADR 0485's "a language is a table row" exists to avoid.
TEST_CASE("A semantic token legend maps the server's own indices onto what this editor colours")
{
	const auto legend = Graphite::ParseSemanticLegend(nlohmann::json::parse(
		R"({"capabilities":{"semanticTokensProvider":{"legend":{"tokenTypes":
			 ["variable","macro","class","function","namespace","parameter","property"]}}}})"));

	REQUIRE(legend.size() == 7);
	CHECK(legend[0] == Graphite::SemanticKind::Other);     // variable - the lexer's colour stands
	CHECK(legend[1] == Graphite::SemanticKind::Macro);
	CHECK(legend[2] == Graphite::SemanticKind::Type);      // class is a type
	CHECK(legend[3] == Graphite::SemanticKind::Function);
	CHECK(legend[4] == Graphite::SemanticKind::Namespace);
	CHECK(legend[5] == Graphite::SemanticKind::Parameter);
	CHECK(legend[6] == Graphite::SemanticKind::Member);    // property is a member
}

TEST_CASE("A server that offers no semantic tokens yields an empty legend rather than a wrong one")
{
	CHECK(Graphite::ParseSemanticLegend(nlohmann::json::parse(R"({"capabilities":{}})")).empty());
	CHECK(Graphite::ParseSemanticLegend(nlohmann::json()).empty());
}

// The reply is a flat run of integers in fives, each relative to the token before it: the line is
// a delta from the previous token's line, and the column a delta from the previous token's column
// only while the line has not changed. Getting the reset-on-new-line wrong shifts every token
// after the first line break, which looks like the colours sliding rather than like a decoding
// bug.
TEST_CASE("A semantic token reply decodes its deltas into absolute positions")
{
	const std::vector<Graphite::SemanticKind> legend{
		Graphite::SemanticKind::Type, Graphite::SemanticKind::Function,
		Graphite::SemanticKind::Macro};

	//                     line col len type mod
	const auto tokens = Graphite::ParseSemanticTokens(nlohmann::json::parse(
		R"({"data":[0,5,3,0,0,  0,5,4,1,0,  2,1,6,2,0]})"), legend);

	REQUIRE(tokens.size() == 3);

	CHECK(tokens[0].Line == 0);
	CHECK(tokens[0].Column == Graphite::Utf16Column(5));
	CHECK(tokens[0].Length == 3);
	CHECK(tokens[0].Kind == Graphite::SemanticKind::Type);

	// Same line, so the column is 5 further along rather than 5 from the left edge.
	CHECK(tokens[1].Line == 0);
	CHECK(tokens[1].Column == Graphite::Utf16Column(10));
	CHECK(tokens[1].Kind == Graphite::SemanticKind::Function);

	// Two lines down, so the column starts from the left edge again.
	CHECK(tokens[2].Line == 2);
	CHECK(tokens[2].Column == Graphite::Utf16Column(1));
	CHECK(tokens[2].Kind == Graphite::SemanticKind::Macro);
}

TEST_CASE("A semantic token naming an index the legend does not cover is dropped, not guessed")
{
	const std::vector<Graphite::SemanticKind> legend{Graphite::SemanticKind::Type};

	// Type index 7 against a legend of one entry: a server and a legend that disagree is not
	// something to colour by approximation.
	CHECK(Graphite::ParseSemanticTokens(nlohmann::json::parse(R"({"data":[0,0,4,7,0]})"),
									   legend).empty());
}

TEST_CASE("A semantic token reply that is malformed yields nothing rather than failing")
{
	const std::vector<Graphite::SemanticKind> legend{Graphite::SemanticKind::Type};

	// Not a multiple of five: the run is truncated, and half a token is not a token.
	CHECK(Graphite::ParseSemanticTokens(nlohmann::json::parse(R"({"data":[0,0,4]})"), legend).empty());
	CHECK(Graphite::ParseSemanticTokens(nlohmann::json(), legend).empty());
	CHECK(Graphite::ParseSemanticTokens(nlohmann::json::parse(R"({"data":"nope"})"), legend).empty());
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

// A symbol carries two ranges and they mean different things: `range` covers the whole
// declaration, `selectionRange` covers its name. Asking the server about a symbol has to use
// the name, or a constructor - whose declaration begins with its own class's name - resolves
// to the class, and the lens over it counts every mention of the type instead.
TEST_CASE("A symbol's name position is read from selectionRange, not from the declaration's start")
{
	const auto declarations = Graphite::ParseSymbols(nlohmann::json::parse(
		R"([{"name":"Application::Application","kind":9,
			 "range":{"start":{"line":28,"character":1}},
			 "selectionRange":{"start":{"line":28,"character":14}}}])"));

	REQUIRE(declarations.size() == 1);

	// Where the declaration begins: the lens is anchored and indented from this.
	CHECK(declarations[0].Line == 28);
	CHECK(declarations[0].Column == Graphite::Utf16Column(1));

	// Where its name begins: this is what the server is asked about.
	CHECK(declarations[0].NameLine == 28);
	CHECK(declarations[0].NameColumn == Graphite::Utf16Column(14));
}

TEST_CASE("A symbol with no selectionRange falls back to its declaration's start")
{
	// The flat SymbolInformation shape carries only location.range. A server that answers in
	// it still has to yield a position that can be asked about.
	const auto declarations = Graphite::ParseSymbols(nlohmann::json::parse(
		R"([{"name":"Draw","kind":12,"range":{"start":{"line":7,"character":3}}}])"));

	REQUIRE(declarations.size() == 1);
	CHECK(declarations[0].NameLine == 7);
	CHECK(declarations[0].NameColumn == Graphite::Utf16Column(3));
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
	CHECK(declarations[0].Column == Graphite::Utf16Column{2});
}

TEST_CASE("A column conversion past the end of its line stops at the end")
{
	CHECK(Graphite::ToUtf16Column("ab", 99) == Graphite::Utf16Column{2});
	CHECK(Graphite::FromUtf16Column("ab", Graphite::Utf16Column{99}) == 2);
	CHECK(Graphite::ToUtf16Column("", 0) == Graphite::Utf16Column{0});
	CHECK(Graphite::FromUtf16Column("", Graphite::Utf16Column{0}) == 0);
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

// A declaration is a reference to itself as far as the protocol is concerned, so asking for
// every site and then dropping the site asked from is what makes the count over a definition
// and the list opened from it agree — and keeps both from leading with the line already on
// screen. Asking for uses only would drop the header declaration too, which is a place worth
// going to rather than noise.
TEST_CASE("A references request asks for every site, declaration included")
{
	TempDirectory temp;
	Graphite::LspClient client = Offline(temp.GetPath());

	// Answered first: before the handshake completes a request is queued rather than sent, so
	// asking without this inspects an empty outbound queue and proves nothing.
	client.Deliver(R"({"jsonrpc":"2.0","id":1,"result":{}})");
	client.TakeSent();

	Graphite::FilePosition where;
	where.Path = temp.GetPath() / "main.cpp";
	where.Line = 28;
	where.Column = Graphite::Utf16Column(14);

	client.AskReferences(where);

	const std::vector<std::string> sent = client.TakeSent();

	REQUIRE(sent.size() == 1);
	CHECK(sent[0].find(R"("method":"textDocument/references")") != std::string::npos);
	CHECK(sent[0].find(R"("includeDeclaration":true)") != std::string::npos);
}

TEST_CASE("A references answer leaves out the position it was asked from, and nothing else")
{
	TempDirectory temp;
	Graphite::LspClient client = Offline(temp.GetPath());

	client.Deliver(R"({"jsonrpc":"2.0","id":1,"result":{}})");

	const std::filesystem::path source = temp.GetPath() / "Application.cpp";

	Graphite::FilePosition where;
	where.Path = source;
	where.Line = 28;
	where.Column = Graphite::Utf16Column(14);

	const Graphite::RequestId id = client.AskReferences(where);

	// Three sites, as clangd answers for a constructor: the definition asked from, the
	// declaration in the header, and one real use.
	client.Deliver(
		R"({"id":)" + std::to_string(id) +
		R"(,"result":[)"
		R"({"uri":")" + Graphite::ToFileUri(source) +
		R"(","range":{"start":{"line":28,"character":14}}},)"
		R"({"uri":"file:///D:/Application.h","range":{"start":{"line":39,"character":11}}},)"
		R"({"uri":"file:///D:/Main.cpp","range":{"start":{"line":54,"character":10}}}]})");

	std::vector<Graphite::LspClient::Answer> answers;
	Graphite::ServerStatus status;
	client.Drain(answers, status);

	REQUIRE(answers.size() == 1);

	// Two, not three: the header declaration stays, because it is somewhere to go; the
	// definition asked from does not, because it is where the question came from.
	REQUIRE(answers[0].Locations.size() == 2);
	CHECK(answers[0].Locations[0].Line == 39);
	CHECK(answers[0].Locations[1].Line == 54);
}

TEST_CASE("A server declaring no semantic tokens is never asked for any")
{
	TempDirectory temp;
	Graphite::LspClient client = Offline(temp.GetPath());

	// A handshake that mentions no semanticTokensProvider - which is every server that does not
	// offer them, and clangd itself before it grew the feature.
	client.Deliver(R"({"jsonrpc":"2.0","id":1,"result":{"capabilities":{}}})");
	client.TakeSent();

	CHECK(client.AskSemanticTokens(temp.GetPath() / "main.cpp") == 0);
	CHECK(client.TakeSent().empty());
}

TEST_CASE("A server declaring a legend is asked, and its reply is read through that legend")
{
	TempDirectory temp;
	Graphite::LspClient client = Offline(temp.GetPath());

	client.Deliver(R"({"jsonrpc":"2.0","id":1,"result":{"capabilities":{"semanticTokensProvider":
				   {"legend":{"tokenTypes":["variable","class"]}}}}})");
	client.TakeSent();

	const Graphite::RequestId id = client.AskSemanticTokens(temp.GetPath() / "main.cpp");

	REQUIRE(id != 0);

	const std::vector<std::string> sent = client.TakeSent();
	REQUIRE(sent.size() == 1);
	CHECK(sent[0].find(R"("method":"textDocument/semanticTokens/full")") != std::string::npos);

	// Index 1 is "class" in this server's numbering, which this editor colours as a type. A
	// client that had hard-coded the protocol's own order would read it as something else.
	client.Deliver(R"({"id":)" + std::to_string(id) + R"(,"result":{"data":[3,2,11,1,0]}})");

	std::vector<Graphite::LspClient::Answer> answers;
	Graphite::ServerStatus status;
	client.Drain(answers, status);

	REQUIRE(answers.size() == 1);
	REQUIRE(answers[0].SemanticTokens.size() == 1);
	CHECK(answers[0].SemanticTokens[0].Line == 3);
	CHECK(answers[0].SemanticTokens[0].Column == Graphite::Utf16Column(2));
	CHECK(answers[0].SemanticTokens[0].Length == 11);
	CHECK(answers[0].SemanticTokens[0].Kind == Graphite::SemanticKind::Type);
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

TEST_CASE("A refused handshake stops the pre-handshake queue growing")
{
	TempDirectory temp;
	temp.MakeFile("compile_flags.txt", "-std=c++20\n");

	Graphite::LspClient client = Offline(temp.GetPath());
	client.TakeSent();

	const std::filesystem::path file = temp.GetPath() / "main.cpp";
	const std::string text(4096, 'x');

	client.OpenDocument(file, text);

	CHECK(client.QueuedCount() == 1);

	client.Deliver(R"({"jsonrpc":"2.0","id":1,"error":{"code":-32603,"message":"no such flag"}})");

	CHECK(client.QueuedCount() == 0);

	// Typing at a server that refused to start used to append a whole copy of the file to the
	// queue on every keystroke, for as long as the tab stayed open.
	for (int keystroke = 0; keystroke < 20; ++keystroke)
	{
		client.ChangeDocument(file, text);
	}

	CHECK(client.QueuedCount() == 0);
}

TEST_CASE("A handshake nobody answers becomes Failed once its time is up")
{
	TempDirectory temp;
	temp.MakeFile("compile_flags.txt", "-std=c++20\n");

	Graphite::LspClient client = Offline(temp.GetPath());
	client.TakeSent();

	client.OpenDocument(temp.GetPath() / "main.cpp", "int Main() { return 0; }\n");

	// Still waiting: the server is entitled to take its time, and Starting is what the status bar
	// should keep saying until the bound is reached.
	CHECK(StatusOf(client).State == Graphite::ServerState::Starting);
	CHECK(client.QueuedCount() == 1);

	// The test's own clock rather than the real one: waiting out the real bound would cost the
	// suite half a minute and prove nothing extra.
	client.ExpireHandshakeIfStalled(std::chrono::steady_clock::now() +
									Graphite::LspClient::kHandshakeTimeout +
									std::chrono::seconds(1));

	CHECK(StatusOf(client).State == Graphite::ServerState::Failed);
	CHECK(client.QueuedCount() == 0);

	client.ChangeDocument(temp.GetPath() / "main.cpp", "int Main() { return 1; }\n");

	CHECK(client.QueuedCount() == 0);
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

	const Graphite::RequestId definition = client.AskDefinition({source, 1, Graphite::Utf16Column{21}});
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

	const Graphite::RequestId id = client.AskReferences({source, 0, Graphite::Utf16Column{4}});
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

	const Graphite::RequestId id = client.AskDefinition({temp.GetPath() / "main.cpp", 0, Graphite::Utf16Column{0}});

	std::vector<Graphite::LspClient::Answer> answers;
	Graphite::ServerStatus status;
	client.Drain(answers, status);

	CHECK(status.State == Graphite::ServerState::Failed);
	CHECK(status.Name == "clangd");

	REQUIRE(answers.size() == 1);
	CHECK(answers[0].Id == id);
	CHECK(answers[0].Failed);
}

TEST_CASE("Nothing is queued for a server that is already gone")
{
	// The leak this closes is silent and unbounded: requests fail fast because a ticket comes
	// back, but a notification has no ticket, so a session that keeps typing at a dead server
	// would pile up a whole file's text per keystroke for as long as it ran.
	TempDirectory temp;

	Graphite::LspClient client{*Graphite::ServerConfigFor(Graphite::LanguageId::Cpp),
							   "no-such-language-server-4c1e.exe", temp.GetPath()};

	const std::filesystem::path source = temp.GetPath() / "main.cpp";

	client.OpenDocument(source, "int Main() { return 0; }\n");

	for (int edit = 0; edit < 50; ++edit)
	{
		client.ChangeDocument(source, std::string(4096, 'x'));
	}

	client.SaveDocument(source);
	client.CloseDocument(source);

	// The client with no process never started a writer either, so this reads the queue directly.
	CHECK(client.TakeSent().empty());
}

TEST_CASE("Nothing can steal the outbound queue from a running writer")
{
	// cmd.exe is a live process with a writer thread behind it. Reading the queue here would take
	// messages the writer was about to send, and losing protocol traffic has no symptom at all —
	// so the answer is nothing, rather than whatever happened to still be sitting there.
	TempDirectory temp;

	Graphite::LspClient client{*Graphite::ServerConfigFor(Graphite::LanguageId::Cpp), "cmd.exe",
							   temp.GetPath()};

	client.OpenDocument(temp.GetPath() / "main.cpp", "int Main() { return 0; }\n");

	CHECK(client.TakeSent().empty());
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
	const Graphite::RequestId id = client.AskDefinition({source, 1, Graphite::Utf16Column{21}});

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
