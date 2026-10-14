#include <doctest/doctest.h>

#include "Core/App/Redraw.h"
#include "Core/Text/Columns.h"
#include "Core/Text/TextBuffer.h"
#include "Language/Registry/LanguageRegistry.h"
#include "Language/Syntax/ColourLines.h"
#include "Language/Syntax/SyntaxCache.h"
#include "Language/Syntax/SyntaxTree.h"
#include "SyntaxFixtures.h"

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

namespace {

	Graphite::SyntaxCache CppCache()
	{
		Graphite::SyntaxCache cache;
		cache.SetSyntax(Graphite::SyntaxOf(Graphite::LanguageId::Cpp));
		return cache;
	}

	class CountingLexer final : public Graphite::Lexer
	{
	public:
		Graphite::LineState LexLine(std::string_view line, const Graphite::LineState& entry,
									std::vector<Graphite::Token>& out) const override
		{
			++m_Calls;

			if (!line.empty())
			{
				out.push_back(Graphite::Token{Graphite::TokenKind::Identifier, 0, line.size()});
			}

			return entry;
		}

		std::size_t GetCallCount() const { return m_Calls; }

	private:
		mutable std::size_t m_Calls = 0;
	};

	std::string KindsOf(Graphite::SyntaxCache& cache, const Core::TextBuffer& buffer,
						std::size_t line)
	{
		std::string kinds;

		for (const Graphite::Token& token : cache.TokensOf(buffer, line))
		{
			kinds += token.Kind == Graphite::TokenKind::Comment	  ? 'm'
					 : token.Kind == Graphite::TokenKind::Keyword ? 'k'
					 : token.Kind == Graphite::TokenKind::String  ? 's'
																  : 'o';
		}

		return kinds;
	}
}

TEST_CASE("SyntaxCache without a lexer reports no tokens")
{
	const Core::TextBuffer buffer{"int x;"};
	Graphite::SyntaxCache cache;

	CHECK_FALSE(cache.HasSyntax());
	CHECK(cache.TokensOf(buffer, 0).empty());
}

TEST_CASE("SyntaxCache lexes a line on demand")
{
	const Core::TextBuffer buffer{"int x;"};
	Graphite::SyntaxCache cache = CppCache();

	CHECK(KindsOf(cache, buffer, 0) == "koo");
}

TEST_CASE("SyntaxCache carries state down from lines never asked about")
{
	const Core::TextBuffer buffer{"/* open\na\nb\nc */ int"};
	Graphite::SyntaxCache cache = CppCache();

	CHECK(KindsOf(cache, buffer, 3) == "mk");
	CHECK(KindsOf(cache, buffer, 1) == "m");
}

TEST_CASE("SyntaxCache answers repeatedly without changing its answer")
{
	const Core::TextBuffer buffer{"/* open\na\nb"};
	Graphite::SyntaxCache cache = CppCache();

	CHECK(KindsOf(cache, buffer, 2) == "m");
	CHECK(KindsOf(cache, buffer, 2) == "m");
	CHECK(KindsOf(cache, buffer, 0) == "m");
}

TEST_CASE("SyntaxCache re-lexes the lines below an invalidated one")
{
	Core::TextBuffer buffer{"/* open\ntext"};
	Graphite::SyntaxCache cache = CppCache();

	CHECK(KindsOf(cache, buffer, 1) == "m");

	buffer.Erase(0, 7);
	buffer.Insert(0, "int x;");
	cache.Invalidate(0);

	CHECK(KindsOf(cache, buffer, 1) == "o");
}

TEST_CASE("SyntaxCache drops everything when the revision moved without an invalidation")
{
	Core::TextBuffer buffer{"/* open\ntext"};
	Graphite::SyntaxCache cache = CppCache();

	CHECK(KindsOf(cache, buffer, 1) == "m");

	buffer.Erase(0, 7);
	buffer.Insert(0, "int x;");

	CHECK(KindsOf(cache, buffer, 1) == "o");
}

TEST_CASE("SyntaxCache clamps a line past the end")
{
	const Core::TextBuffer buffer{"int x;"};
	Graphite::SyntaxCache cache = CppCache();

	CHECK(cache.TokensOf(buffer, 99).empty());
}

TEST_CASE("SyntaxCache re-lexes when the tab width changes")
{
	const Core::TextBuffer buffer{"\tint x;"};
	Graphite::SyntaxCache cache = CppCache();

	cache.SetTabWidth(4);
	const Graphite::Token firstAtFour = cache.TokensOf(buffer, 0).front();

	cache.SetTabWidth(8);
	const Graphite::Token firstAtEight = cache.TokensOf(buffer, 0).front();

	CHECK(firstAtFour.From == 4);
	CHECK(firstAtEight.From == 8);
	CHECK(firstAtEight.To - firstAtEight.From == firstAtFour.To - firstAtFour.From);
}

TEST_CASE("SyntaxCache keeps its states when the tab width is unchanged")
{
	const Core::TextBuffer buffer{"/* open\nstill"};
	CountingLexer lexer;
	Graphite::SyntaxCache cache;
	cache.SetSyntax(Graphite::SyntaxSource{.LineLexer = &lexer});

	cache.SetTabWidth(4);
	cache.TokensOf(buffer, 1);
	const std::size_t callsAfterFirst = lexer.GetCallCount();

	cache.SetTabWidth(4);
	cache.TokensOf(buffer, 1);
	CHECK(lexer.GetCallCount() == callsAfterFirst);

	cache.SetTabWidth(8);
	cache.TokensOf(buffer, 1);
	CHECK(lexer.GetCallCount() > callsAfterFirst);
}

namespace {

	Graphite::SyntaxCache CSharpCache()
	{
		Graphite::SyntaxCache cache;
		cache.SetSyntax(Graphite::SyntaxOf(Graphite::LanguageId::CSharp));
		cache.SetParseBudget(Graphite::ParseBudget{.PerAttempt = std::chrono::seconds{10}, .PauseAfterRunningOut = std::chrono::microseconds{0}});
		return cache;
	}

	std::string DescribeLine(Graphite::SyntaxCache& cache, const Core::TextBuffer& buffer, std::size_t line)
	{
		const std::string expanded = Core::ExpandTabs(buffer.LineText(line), cache.GetTabWidth());
		return DescribeTokens(expanded, cache.TokensOf(buffer, line));
	}
}

TEST_CASE("A row too long to colour from its tree is drawn plain while its neighbours keep their colours")
{
	const std::string longRow = "var s = \"" + std::string(Graphite::kLongestTreeColouredRow + 1, 'x') + "\";";
	const Core::TextBuffer buffer{"class A {}\n" + longRow + "\nclass B {}\n"};
	Graphite::SyntaxCache cache = CSharpCache();

	CHECK(cache.TokensOf(buffer, 1).empty());
	CHECK_FALSE(cache.TokensOf(buffer, 0).empty());
	CHECK_FALSE(cache.TokensOf(buffer, 2).empty());
}

TEST_CASE("SyntaxCache colours a C# document through its grammar")
{
	const Core::TextBuffer buffer{std::string(kCSharpFixture)};
	Graphite::SyntaxCache cache = CSharpCache();

	CHECK(cache.HasSyntax());
	CHECK(DescribeLine(cache, buffer, 5) == "keyword 'public' | keyword 'sealed' | keyword 'class' | type 'Widget' | punctuation '<' | type 'T' | punctuation '>' | punctuation ':' | type 'IDisposable' | keyword 'where' | type 'T' | punctuation ':' | keyword 'class'");
	CHECK(DescribeLine(cache, buffer, 3) == "comment '   line */'");
}

TEST_CASE("SyntaxCache puts a tree's tokens on the tab-expanded line at any tab width")
{
	const Core::TextBuffer buffer{std::string("\tint x = 1;")};
	Graphite::SyntaxCache cache = CSharpCache();

	CHECK(DescribeLine(cache, buffer, 0) == "keyword 'int' | identifier 'x' | punctuation '=' | number '1' | punctuation ';'");
	CHECK(cache.TokensOf(buffer, 0).front().From == 4);

	cache.SetTabWidth(8);

	CHECK(cache.TokensOf(buffer, 0).front().From == 8);
	CHECK(DescribeLine(cache, buffer, 0) == "keyword 'int' | identifier 'x' | punctuation '=' | number '1' | punctuation ';'");
}

TEST_CASE("SyntaxCache colours a C# line anew after an edit, and again after its undo")
{
	Core::TextBuffer buffer{std::string("int x = 1;\nint y = 2;")};
	Graphite::SyntaxCache cache = CSharpCache();
	const std::string declaration = "keyword 'int' | identifier 'y' | punctuation '=' | number '2' | punctuation ';'";

	REQUIRE(DescribeLine(cache, buffer, 1) == declaration);

	buffer.Insert(buffer.LineStart(1), "// ");
	CHECK(DescribeLine(cache, buffer, 1) == "comment '// int y = 2;'");

	REQUIRE(buffer.Undo().has_value());
	CHECK(DescribeLine(cache, buffer, 1) == declaration);
}

TEST_CASE("A tree-coloured line past the end of the buffer has no tokens")
{
	const Core::TextBuffer buffer{std::string("int x;")};
	Graphite::SyntaxCache cache = CSharpCache();

	CHECK(cache.TokensOf(buffer, 5).empty());
}

TEST_CASE("SyntaxCache and ColourLines colour the same C# text alike")
{
	const Core::TextBuffer buffer{std::string(kCSharpFixture)};
	Graphite::SyntaxCache cache = CSharpCache();
	std::vector<std::string_view> rows;

	for (std::size_t row = 0; row < buffer.GetLineCount(); ++row)
	{
		rows.push_back(FixtureRow(kCSharpFixture, row));
	}

	const std::vector<std::vector<Graphite::Token>> coloured = Graphite::ColourLines(Graphite::LanguageId::CSharp, rows);

	REQUIRE(coloured.size() == buffer.GetLineCount());

	for (std::size_t row = 0; row < buffer.GetLineCount(); ++row)
	{
		INFO("row ", row);
		CHECK(cache.TokensOf(buffer, row) == coloured[row]);
	}
}

TEST_CASE("While a parse is out of time the old colours stay, clipped to the line as it is now")
{
	Core::TextBuffer buffer{std::string("string s = \"abcdef\";")};
	Graphite::SyntaxCache cache = CSharpCache();

	REQUIRE(DescribeLine(cache, buffer, 0) == "keyword 'string' | identifier 's' | punctuation '=' | string '\"abcdef\"' | punctuation ';'");

	buffer.Erase(0, buffer.GetLength());
	buffer.Insert(0, "int s = 1;\n" + ManyCSharpClasses(2000));

	cache.SetParseBudget(Graphite::ParseBudget{.PerAttempt = std::chrono::microseconds{0}, .PauseAfterRunningOut = std::chrono::hours{1}});

	const std::vector<Graphite::Token> lagging{
		Graphite::Token{.Kind = Graphite::TokenKind::Keyword, .From = 0, .To = 6},
		Graphite::Token{.Kind = Graphite::TokenKind::Identifier, .From = 7, .To = 8},
		Graphite::Token{.Kind = Graphite::TokenKind::Punctuation, .From = 9, .To = 10}};

	CHECK(cache.TokensOf(buffer, 0) == lagging);
	CHECK(cache.TokensOf(buffer, 0) == lagging);

	cache.SetParseBudget(Graphite::ParseBudget{.PerAttempt = std::chrono::seconds{10}, .PauseAfterRunningOut = std::chrono::microseconds{0}});

	CHECK(DescribeLine(cache, buffer, 0) == "keyword 'int' | identifier 's' | punctuation '=' | number '1' | punctuation ';'");
}

TEST_CASE("Undo while a parse is pending shows the finished tree's colours, and redo the edited text's")
{
	Core::TextBuffer buffer{ManyCSharpClasses(2000)};
	Graphite::SyntaxCache cache = CSharpCache();
	constexpr std::size_t kEditedLine = 5;

	const std::vector<Graphite::Token> finished = cache.TokensOf(buffer, kEditedLine);
	REQUIRE_FALSE(finished.empty());

	const std::size_t lineEnd = buffer.LineStart(kEditedLine) + buffer.LineText(kEditedLine).size();
	buffer.Erase(lineEnd - 10, lineEnd);

	cache.SetParseBudget(Graphite::ParseBudget{.PerAttempt = std::chrono::microseconds{0}, .PauseAfterRunningOut = std::chrono::hours{1}});
	REQUIRE(cache.TokensOf(buffer, kEditedLine) != finished);

	REQUIRE(buffer.Undo().has_value());
	CHECK(cache.TokensOf(buffer, kEditedLine) == finished);

	cache.SetParseBudget(Graphite::ParseBudget{.PerAttempt = std::chrono::seconds{10}, .PauseAfterRunningOut = std::chrono::microseconds{0}});
	REQUIRE(buffer.Redo().has_value());

	const Core::TextBuffer edited{buffer.Materialize()};
	Graphite::SyntaxCache fresh = CSharpCache();

	for (std::size_t line = kEditedLine - 1; line <= kEditedLine + 1; ++line)
	{
		INFO("line ", line);
		CHECK(cache.TokensOf(buffer, line) == fresh.TokensOf(edited, line));
	}

	CHECK(cache.TokensOf(buffer, kEditedLine) != finished);
}

TEST_CASE("A document's first parse, out of time, finishes over later requests")
{
	const Core::TextBuffer buffer{ManyCSharpClasses(2000)};
	Graphite::SyntaxCache cache = CSharpCache();
	cache.SetParseBudget(Graphite::ParseBudget{.PerAttempt = std::chrono::microseconds{0}, .PauseAfterRunningOut = std::chrono::microseconds{0}});

	CHECK(cache.TokensOf(buffer, 5).empty());

	std::size_t requests = 1;

	while (cache.TokensOf(buffer, 5).empty() && requests < 1'000'000)
	{
		++requests;
	}

	CHECK(requests > 1);
	CHECK(DescribeLine(cache, buffer, 5) == "keyword 'class' | type 'C5' | punctuation '{' | keyword 'int' | identifier 'f' | punctuation '=' | number '1' | punctuation ';' | keyword 'void' | function 'M' | punctuation '(' | punctuation ')' | punctuation '{' | identifier 'f' | punctuation '+=' | number '2' | punctuation ';' | punctuation '}' | punctuation '}'");
}

TEST_CASE("Switching a cache from a grammar to a lexer colours by the lexer")
{
	const Core::TextBuffer buffer{std::string("/* a */ int x;")};
	Graphite::SyntaxCache cache = CSharpCache();

	REQUIRE_FALSE(cache.TokensOf(buffer, 0).empty());

	cache.SetSyntax(Graphite::SyntaxOf(Graphite::LanguageId::Cpp));

	CHECK(cache.TokensOf(buffer, 0) == LexCppLine("/* a */ int x;"));

	cache.SetSyntax(Graphite::SyntaxSource{});

	CHECK_FALSE(cache.HasSyntax());
	CHECK(cache.TokensOf(buffer, 0).empty());
}

TEST_CASE("A tab-width change re-expands a tree's tokens without parsing again")
{
	const Core::TextBuffer buffer{std::string("\tint x = 1;")};
	Graphite::SyntaxCache cache = CSharpCache();

	REQUIRE(cache.TokensOf(buffer, 0).front().From == 4);

	cache.SetParseBudget(Graphite::ParseBudget{.PerAttempt = std::chrono::microseconds{0}, .PauseAfterRunningOut = std::chrono::hours{1}});
	cache.SetTabWidth(8);

	CHECK(cache.TokensOf(buffer, 0).front().From == 8);
}

TEST_CASE("Running out of parse time asks for a frame when the pause ends")
{
	const Core::TextBuffer buffer{ManyCSharpClasses(2000)};
	Graphite::SyntaxCache cache = CSharpCache();
	cache.SetParseBudget(Graphite::ParseBudget{.PerAttempt = std::chrono::microseconds{0}, .PauseAfterRunningOut = std::chrono::hours{1}});
	Core::Redraw::TakeDeadline();

	CHECK(cache.TokensOf(buffer, 5).empty());

	CHECK(Core::Redraw::TakeDeadline().has_value());
}

TEST_CASE("A frame that lands during the pause asks again for a frame when the pause ends")
{
	const Core::TextBuffer buffer{ManyCSharpClasses(2000)};
	Graphite::SyntaxCache cache = CSharpCache();
	cache.SetParseBudget(Graphite::ParseBudget{.PerAttempt = std::chrono::microseconds{0}, .PauseAfterRunningOut = std::chrono::hours{1}});
	Core::Redraw::TakeDeadline();

	CHECK(cache.TokensOf(buffer, 5).empty());
	REQUIRE(Core::Redraw::TakeDeadline().has_value());

	CHECK(cache.TokensOf(buffer, 5).empty());

	CHECK(Core::Redraw::TakeDeadline().has_value());
}

TEST_CASE("A text too large to parse has no tokens until it shrinks under the limit")
{
	Core::TextBuffer buffer{std::string(Graphite::kLargestParsedText, ' ') + "\nint x;"};
	Graphite::SyntaxCache cache = CSharpCache();

	CHECK(cache.TokensOf(buffer, 1).empty());
	CHECK(cache.TokensOf(buffer, 1).empty());

	buffer.Erase(0, buffer.GetLength() - 6);

	CHECK(DescribeLine(cache, buffer, 0) == "keyword 'int' | identifier 'x' | punctuation ';'");
}
