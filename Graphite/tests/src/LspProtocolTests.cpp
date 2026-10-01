#include <doctest/doctest.h>

#include "Core/Fs/FileSystem.h"
#include "Lang/Server/LspProtocol.h"

#include <filesystem>
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

TEST_CASE("A publication is parsed into a path, a version and its diagnostics")
{
	const nlohmann::json params = nlohmann::json::parse(R"({
		"uri": "file:///C:/src/Main.cpp",
		"version": 7,
		"diagnostics": [
			{"range": {"start": {"line": 3, "character": 8}, "end": {"line": 3, "character": 14}},
			 "severity": 1, "message": "no matching constructor", "source": "clang"}
		]
	})");

	const Graphite::PublishedDiagnostics published = Graphite::ParseDiagnostics(params);

	CHECK(published.Path == std::filesystem::path{"C:/src/Main.cpp"});
	CHECK(published.Version == Graphite::DocumentVersion{7});
	REQUIRE(published.Items.size() == 1);
	CHECK(published.Items[0].Line == 3);
	CHECK(published.Items[0].Column == Graphite::Utf16Column{8});
	CHECK(published.Items[0].EndLine == 3);
	CHECK(published.Items[0].EndColumn == Graphite::Utf16Column{14});
	CHECK(published.Items[0].Severity == Graphite::DiagnosticSeverity::Error);
	CHECK(published.Items[0].Message == "no matching constructor");
}

TEST_CASE("Only errors and warnings survive the parse")
{
	// Information and Hint are mostly clang-tidy, which can bury a file. They are dropped here
	// rather than filtered by a reader, so nothing downstream has to know they exist.
	const nlohmann::json params = nlohmann::json::parse(R"({
		"uri": "file:///C:/src/Main.cpp",
		"diagnostics": [
			{"range": {"start": {"line": 0, "character": 0}, "end": {"line": 0, "character": 1}},
			 "severity": 1, "message": "error"},
			{"range": {"start": {"line": 1, "character": 0}, "end": {"line": 1, "character": 1}},
			 "severity": 2, "message": "warning"},
			{"range": {"start": {"line": 2, "character": 0}, "end": {"line": 2, "character": 1}},
			 "severity": 3, "message": "information"},
			{"range": {"start": {"line": 3, "character": 0}, "end": {"line": 3, "character": 1}},
			 "severity": 4, "message": "hint"}
		]
	})");

	const Graphite::PublishedDiagnostics published = Graphite::ParseDiagnostics(params);

	REQUIRE(published.Items.size() == 2);
	CHECK(published.Items[0].Severity == Graphite::DiagnosticSeverity::Error);
	CHECK(published.Items[1].Severity == Graphite::DiagnosticSeverity::Warning);
}

TEST_CASE("A diagnostic naming no severity is kept as an error")
{
	// The protocol allows the field to be absent and leaves the meaning to the client. Treating
	// the unknown as the louder of the two is the safe direction: a warning drawn as an error is
	// noticed and corrected, an error drawn as nothing is not.
	const nlohmann::json params = nlohmann::json::parse(R"({
		"uri": "file:///C:/src/Main.cpp",
		"diagnostics": [
			{"range": {"start": {"line": 0, "character": 0}, "end": {"line": 0, "character": 1}},
			 "message": "unclassified"}
		]
	})");

	const Graphite::PublishedDiagnostics published = Graphite::ParseDiagnostics(params);

	REQUIRE(published.Items.size() == 1);
	CHECK(published.Items[0].Severity == Graphite::DiagnosticSeverity::Error);
}

TEST_CASE("A publication naming no version reads as version zero")
{
	// clangd sends one; the protocol makes it optional. Zero is "never sent", which compares
	// less than every real version and so reads as stale - the safe direction again.
	const nlohmann::json params = nlohmann::json::parse(R"({
		"uri": "file:///C:/src/Main.cpp",
		"diagnostics": []
	})");

	const Graphite::PublishedDiagnostics published = Graphite::ParseDiagnostics(params);

	CHECK(published.Version == Graphite::DocumentVersion{});
	CHECK(published.Items.empty());
	CHECK(published.Path == std::filesystem::path{"C:/src/Main.cpp"});
}

TEST_CASE("A publication with no usable uri yields an empty path")
{
	// ADR 0004 forbids throwing across this boundary, and the existing parsers tolerate null, a
	// wrong type and missing fields by yielding less. An empty path is what a caller drops on.
	CHECK(Graphite::ParseDiagnostics(nlohmann::json::object()).Path.empty());
	CHECK(Graphite::ParseDiagnostics(nlohmann::json{{"uri", 7}}).Path.empty());
	CHECK(Graphite::ParseDiagnostics(nullptr).Path.empty());
}

TEST_CASE("A range with no end collapses onto its start")
{
	const nlohmann::json params = nlohmann::json::parse(R"({
		"uri": "file:///C:/src/Main.cpp",
		"diagnostics": [
			{"range": {"start": {"line": 5, "character": 2}}, "severity": 2, "message": "odd"}
		]
	})");

	const Graphite::PublishedDiagnostics published = Graphite::ParseDiagnostics(params);

	REQUIRE(published.Items.size() == 1);
	CHECK(published.Items[0].EndLine == 5);
	CHECK(published.Items[0].EndColumn == Graphite::Utf16Column{2});
}
