#include <doctest/doctest.h>

#include "Lang/Server/LspClient.h"
#include "Lang/Server/ServerDiscovery.h"
#include "Lang/Server/ServerRegistry.h"
#include "TempDirectory.h"

#include <chrono>
#include <fstream>
#include <thread>

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
		Graphite::LspClient::Drained drained;
		client.Drain(drained);

		return drained.Status;
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

	Graphite::LspClient::Drained drained;
	client.Drain(drained);
	const std::vector<Graphite::LspClient::Answer>& answers = drained.Answers;

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

	Graphite::LspClient::Drained drained;
	client.Drain(drained);
	const std::vector<Graphite::LspClient::Answer>& answers = drained.Answers;

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

TEST_CASE("Indexing ends only when the last of its progress tokens ends")
{
	// clangd can run more than one piece of work at once, each under its own token - the
	// background index and a file's own build, say. The first to finish says nothing about the
	// others, and reading its end as "indexing is over" turns the status back to Ready while the
	// index the answers come from is still partial.
	TempDirectory temp;
	temp.MakeFile("compile_flags.txt", "-std=c++20\n");

	Graphite::LspClient client = Offline(temp.GetPath());
	client.Deliver(R"({"jsonrpc":"2.0","id":1,"result":{}})");

	client.Deliver(
		R"({"method":"$/progress","params":{"token":"index","value":{"kind":"begin","title":"indexing"}}})");
	client.Deliver(
		R"({"method":"$/progress","params":{"token":7,"value":{"kind":"begin","title":"building"}}})");

	client.Deliver(R"({"method":"$/progress","params":{"token":7,"value":{"kind":"end"}}})");

	CHECK(StatusOf(client).State == Graphite::ServerState::Indexing);

	// The same spelling of a different token - a string "7" is not the integer 7 - ends nothing.
	client.Deliver(R"({"method":"$/progress","params":{"token":"7","value":{"kind":"end"}}})");

	CHECK(StatusOf(client).State == Graphite::ServerState::Indexing);

	client.Deliver(R"({"method":"$/progress","params":{"token":"index","value":{"kind":"end"}}})");

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

TEST_CASE("A server request under a string id is answered under that same id")
{
	// JSON-RPC lets an id be a string, and a server that numbers its requests that way waits
	// forever for an answer if the id is dropped or rewritten - the reply has to carry back
	// exactly what arrived.
	TempDirectory temp;
	Graphite::LspClient client = Offline(temp.GetPath());
	client.TakeSent();

	client.Deliver(R"({"id":"cfg-1","method":"workspace/configuration","params":{"items":[{}]}})");
	client.Deliver(R"({"id":"7","method":"workspace/applyEdit","params":{}})");

	const std::vector<std::string> sent = client.TakeSent();

	REQUIRE(sent.size() == 2);
	CHECK(sent[0].find(R"("id":"cfg-1")") != std::string::npos);
	CHECK(sent[0].find(R"("result":[{}])") != std::string::npos);

	// Still the string "7", not the number it looks like.
	CHECK(sent[1].find(R"("id":"7")") != std::string::npos);
	CHECK(sent[1].find(R"("code":-32601)") != std::string::npos);
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

	Graphite::LspClient::Drained drained;
	client.Drain(drained);
	const std::vector<Graphite::LspClient::Answer>& answers = drained.Answers;

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

	Graphite::LspClient::Drained drained;
	client.Drain(drained);
	const std::vector<Graphite::LspClient::Answer>& answers = drained.Answers;

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

	Graphite::LspClient::Drained drained;
	client.Drain(drained);
	const std::vector<Graphite::LspClient::Answer>& answers = drained.Answers;

	CHECK(drained.Status.State == Graphite::ServerState::Failed);
	CHECK(drained.Status.Name == "clangd");

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

	Graphite::LspClient::Drained drained;
	client.Drain(drained);
	const std::vector<Graphite::LspClient::Answer>& answers = drained.Answers;

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

	Graphite::LspClient::Drained drained;

	for (int attempt = 0; attempt < 200 && drained.Answers.empty(); ++attempt)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		client.Drain(drained);
	}

	const std::vector<Graphite::LspClient::Answer>& answers = drained.Answers;

	REQUIRE(answers.size() == 1);
	CHECK(answers[0].Id == id);
	REQUIRE(answers[0].Locations.size() == 1);
	CHECK(answers[0].Locations[0].Line == 0);
}

TEST_CASE("A publication for an open document leaves through Drain")
{
	Graphite::LspClient client = Offline("C:/src");

	client.OpenDocument("C:/src/Main.cpp", "int main() {}\n");

	client.Deliver(R"({"jsonrpc":"2.0","method":"textDocument/publishDiagnostics","params":{
		"uri":"file:///C:/src/Main.cpp","version":1,
		"diagnostics":[{"range":{"start":{"line":0,"character":4},
			"end":{"line":0,"character":8}},"severity":1,"message":"boom"}]}})");

	Graphite::LspClient::Drained drained;
	client.Drain(drained);

	REQUIRE(drained.Diagnostics.size() == 1);
	CHECK(drained.Diagnostics[0].Path == std::filesystem::path{"C:/src/Main.cpp"});
	CHECK(drained.Diagnostics[0].Version == Graphite::DocumentVersion{1});
	REQUIRE(drained.Diagnostics[0].Items.size() == 1);
	CHECK(drained.Diagnostics[0].Items[0].Message == "boom");
}

TEST_CASE("A publication for a document this client never opened is dropped")
{
	// Not hypothetical. The warm-up in LanguageServices::Poll opens a representative file and
	// closes it immediately, and a publication for it can already be in flight when the close
	// goes out; preview panes open and close documents the same way. Without this the model
	// collects entries for files nothing on screen corresponds to.
	Graphite::LspClient client = Offline("C:/src");

	client.Deliver(R"({"jsonrpc":"2.0","method":"textDocument/publishDiagnostics","params":{
		"uri":"file:///C:/src/Never.cpp","version":1,
		"diagnostics":[{"range":{"start":{"line":0,"character":0},
			"end":{"line":0,"character":1}},"severity":1,"message":"boom"}]}})");

	Graphite::LspClient::Drained drained;
	client.Drain(drained);

	CHECK(drained.Diagnostics.empty());
}

TEST_CASE("A publication arriving after the document was closed is dropped")
{
	Graphite::LspClient client = Offline("C:/src");

	client.OpenDocument("C:/src/Main.cpp", "int main() {}\n");
	client.CloseDocument("C:/src/Main.cpp");

	client.Deliver(R"({"jsonrpc":"2.0","method":"textDocument/publishDiagnostics","params":{
		"uri":"file:///C:/src/Main.cpp","version":1,
		"diagnostics":[{"range":{"start":{"line":0,"character":0},
			"end":{"line":0,"character":1}},"severity":1,"message":"boom"}]}})");

	Graphite::LspClient::Drained drained;
	client.Drain(drained);

	CHECK(drained.Diagnostics.empty());
}

TEST_CASE("A publication still waiting to be drained when its document closes is dropped with it")
{
	// Published while the document was open, so the open check let it through, but not yet
	// drained when the close went out. Drained afterwards it would reach DiagnosticsModel in the
	// same update as the close's own note, and the model's close-then-publish order would recreate
	// the entry the close had just erased - with no later close left to erase it again.
	Graphite::LspClient client = Offline("C:/src");

	client.OpenDocument("C:/src/Main.cpp", "int main() {}\n");
	client.OpenDocument("C:/src/Other.cpp", "int other() {}\n");

	for (const char* uri : {"file:///C:/src/Main.cpp", "file:///C:/src/Other.cpp"})
	{
		client.Deliver(std::string(R"({"jsonrpc":"2.0","method":"textDocument/publishDiagnostics","params":{"uri":")") +
					   uri + R"(","version":1,"diagnostics":[{"range":{"start":{"line":0,"character":0},
			"end":{"line":0,"character":1}},"severity":1,"message":"boom"}]}})");
	}

	client.CloseDocument("C:/src/Main.cpp");

	Graphite::LspClient::Drained drained;
	client.Drain(drained);

	// Only the closed document's publication goes; the one still open keeps its own.
	REQUIRE(drained.Diagnostics.size() == 1);
	CHECK(drained.Diagnostics[0].Path == std::filesystem::path{"C:/src/Other.cpp"});
}

TEST_CASE("The version a document is sent under is the one that goes out on the wire")
{
	Graphite::LspClient client = Offline("C:/src");

	const Graphite::DocumentVersion opened = client.OpenDocument("C:/src/Main.cpp", "a\n");
	const Graphite::DocumentVersion changed = client.ChangeDocument("C:/src/Main.cpp", "ab\n");

	CHECK(opened == Graphite::DocumentVersion{1});
	CHECK(changed == Graphite::DocumentVersion{2});

	// A change to a document that was never opened is a change to nothing, and says so.
	CHECK(client.ChangeDocument("C:/src/Other.cpp", "x\n") == Graphite::DocumentVersion{});
}

TEST_CASE("A switch header/source request asks clangd's extension for the document")
{
	TempDirectory temp;
	Graphite::LspClient client = Offline(temp.GetPath());

	client.Deliver(R"({"jsonrpc":"2.0","id":1,"result":{}})");
	client.TakeSent();

	client.AskSwitchSourceHeader(temp.GetPath() / "Layer.h");

	const std::vector<std::string> sent = client.TakeSent();

	REQUIRE(sent.size() == 1);
	CHECK(sent[0].find(R"("method":"textDocument/switchSourceHeader")") != std::string::npos);
	CHECK(sent[0].find("Layer.h") != std::string::npos);
}

TEST_CASE("A switch header/source answer carries the file the server named")
{
	TempDirectory temp;
	Graphite::LspClient client = Offline(temp.GetPath());

	client.Deliver(R"({"jsonrpc":"2.0","id":1,"result":{}})");
	client.TakeSent();

	const Graphite::RequestId id = client.AskSwitchSourceHeader(temp.GetPath() / "Layer.h");
	client.Deliver(R"({"jsonrpc":"2.0","id":)" + std::to_string(id) +
				   R"(,"result":"file:///C:/project/Layer.cpp"})");

	Graphite::LspClient::Drained drained;
	client.Drain(drained);

	REQUIRE(drained.Answers.size() == 1);
	REQUIRE(drained.Answers[0].Locations.size() == 1);
	CHECK(drained.Answers[0].Locations[0].Path ==
		  Graphite::FromFileUri("file:///C:/project/Layer.cpp"));
}

TEST_CASE("The handshake declares markdown hover")
{
	TempDirectory temp;
	Graphite::LspClient client = Offline(temp.GetPath());

	const std::vector<std::string> sent = client.TakeSent();

	REQUIRE(sent.size() == 1);
	constexpr std::string_view kFormats = R"("hover":{"contentFormat":["markdown","plaintext"]})";
	CHECK(sent[0].find(kFormats) != std::string::npos);
}

TEST_CASE("A hover request carries the position and its markdown reply lands in Text")
{
	TempDirectory temp;
	Graphite::LspClient client = Offline(temp.GetPath());

	client.Deliver(R"({"jsonrpc":"2.0","id":1,"result":{}})");
	client.TakeSent();

	Graphite::FilePosition where;
	where.Path = temp.GetPath() / "main.cpp";
	where.Line = 12;
	where.Column = Graphite::Utf16Column(21);

	const Graphite::RequestId id = client.AskHover(where);
	const std::vector<std::string> sent = client.TakeSent();

	REQUIRE(sent.size() == 1);
	CHECK(sent[0].find(R"("method":"textDocument/hover")") != std::string::npos);
	CHECK(sent[0].find(R"("line":12)") != std::string::npos);

	client.Deliver(R"({"jsonrpc":"2.0","id":)" + std::to_string(id) +
				   R"(,"result":{"contents":{"kind":"markdown","value":"### field `x`"}}})");

	Graphite::LspClient::Drained drained;
	client.Drain(drained);

	REQUIRE(drained.Answers.size() == 1);
	CHECK_FALSE(drained.Answers[0].Failed);
	CHECK(drained.Answers[0].Text == "### field `x`");
}

TEST_CASE("A null hover reply is an answer with no text")
{
	TempDirectory temp;
	Graphite::LspClient client = Offline(temp.GetPath());

	client.Deliver(R"({"jsonrpc":"2.0","id":1,"result":{}})");
	client.TakeSent();

	const Graphite::RequestId id = client.AskHover(Graphite::FilePosition{});
	client.Deliver(R"({"jsonrpc":"2.0","id":)" + std::to_string(id) + R"(,"result":null})");

	Graphite::LspClient::Drained drained;
	client.Drain(drained);

	REQUIRE(drained.Answers.size() == 1);
	CHECK_FALSE(drained.Answers[0].Failed);
	CHECK(drained.Answers[0].Text.empty());
}
