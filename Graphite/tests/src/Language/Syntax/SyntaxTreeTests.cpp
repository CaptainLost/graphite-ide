#include <doctest/doctest.h>

#include "Language/Syntax/Grammar.h"
#include "Language/Syntax/Grammars/CSharpGrammar.h"
#include "Language/Syntax/SyntaxTree.h"
#include "SyntaxFixtures.h"

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

namespace {

	std::string DescribeRow(Graphite::SyntaxTree& tree, std::size_t row)
	{
		std::vector<Graphite::Token> tokens;
		tree.AppendRowTokens(row, tokens);
		return DescribeTokens(tree.RowText(row), tokens);
	}

	std::vector<std::string> DescribeEveryRow(Graphite::SyntaxTree& tree)
	{
		std::vector<std::string> rows;

		for (std::size_t row = 0; row < tree.GetRowCount(); ++row)
		{
			rows.push_back(DescribeRow(tree, row));
		}

		return rows;
	}

	std::vector<std::string> DescribeFreshParse(const std::string& text)
	{
		Graphite::SyntaxTree fresh(Graphite::CSharpGrammar());
		fresh.Retarget(text);
		REQUIRE(fresh.AdvanceToEnd());
		return DescribeEveryRow(fresh);
	}

	Graphite::SyntaxTree& ParsedFixture(Graphite::SyntaxTree& tree)
	{
		tree.Retarget(std::string(kCSharpFixture));
		REQUIRE(tree.AdvanceToEnd());
		return tree;
	}

	bool AreAscendingAndApart(const std::vector<Graphite::Token>& tokens, std::size_t rowLength)
	{
		std::size_t previousEnd = 0;

		for (const Graphite::Token& token : tokens)
		{
			const bool startsAfterPrevious = token.From >= previousEnd;
			const bool isInsideRow = token.From < token.To && token.To <= rowLength;

			if (!startsAfterPrevious || !isInsideRow)
			{
				return false;
			}

			previousEnd = token.To;
		}

		return true;
	}
}

TEST_CASE("A C# declaration line is coloured by what each part of it is")
{
	Graphite::SyntaxTree tree(Graphite::CSharpGrammar());
	ParsedFixture(tree);

	CHECK(tree.GetRowCount() == 27);
	CHECK(DescribeRow(tree, 0) == "keyword 'using' | namespace 'System' | punctuation '.' | namespace 'Collections' | punctuation '.' | namespace 'Generic' | punctuation ';'");
	CHECK(DescribeRow(tree, 5) == "keyword 'public' | keyword 'sealed' | keyword 'class' | type 'Widget' | punctuation '<' | type 'T' | punctuation '>' | punctuation ':' | type 'IDisposable' | keyword 'where' | type 'T' | punctuation ':' | keyword 'class'");
	CHECK(DescribeRow(tree, 8) == "keyword 'public' | keyword 'string' | member 'Name' | punctuation '{' | keyword 'get' | punctuation ';' | keyword 'init' | punctuation ';' | punctuation '}' | punctuation '=' | string '@\"C:\\path\"' | punctuation ';'");
	CHECK(DescribeRow(tree, 9) == "keyword 'public' | type 'Widget' | punctuation '(' | keyword 'int' | parameter 'size' | punctuation ')' | punctuation '{' | identifier 'count' | punctuation '=' | identifier 'size' | punctuation ';' | punctuation '}'");
	CHECK(DescribeRow(tree, 18) == "identifier 'Console' | punctuation '.' | function 'WriteLine' | punctuation '(' | identifier 's' | punctuation '+' | identifier 'raw' | punctuation '+' | identifier 'c' | punctuation ')' | punctuation ';'");
}

TEST_CASE("A comment over two rows colours each row")
{
	Graphite::SyntaxTree tree(Graphite::CSharpGrammar());
	ParsedFixture(tree);

	CHECK(DescribeRow(tree, 2) == "comment '/* multi'");
	CHECK(DescribeRow(tree, 3) == "comment '   line */'");
}

TEST_CASE("Interpolated and raw strings colour as strings around the code inside them")
{
	Graphite::SyntaxTree tree(Graphite::CSharpGrammar());
	ParsedFixture(tree);

	CHECK(DescribeRow(tree, 12) == "keyword 'var' | identifier 's' | punctuation '=' | string '$' | string '\"' | string '{' | identifier 'label' | string '}' | string ': ' | string '{' | identifier 'count' | punctuation ',' | number '5' | string '}' | string '\"' | punctuation ';'");
	CHECK(DescribeRow(tree, 13) == "keyword 'var' | identifier 'raw' | punctuation '=' | string '\"\"\"'");
	CHECK(DescribeRow(tree, 14) == "string '          raw \"text\"'");
	CHECK(DescribeRow(tree, 15) == "string '          \"\"\"' | punctuation ';'");
}

TEST_CASE("A character literal and its escape colour as a character")
{
	Graphite::SyntaxTree tree(Graphite::CSharpGrammar());
	ParsedFixture(tree);

	CHECK(DescribeRow(tree, 16) == "keyword 'char' | identifier 'c' | punctuation '=' | character ''' | character '\\n' | character ''' | punctuation ';'");
}

TEST_CASE("A directive keyword and a region's text colour as preprocessor, the rest of the line as code")
{
	Graphite::SyntaxTree tree(Graphite::CSharpGrammar());
	ParsedFixture(tree);

	CHECK(DescribeRow(tree, 20) == "preprocessor '#if' | identifier 'DEBUG'");
	CHECK(DescribeRow(tree, 22) == "preprocessor '#endif'");
	CHECK(DescribeRow(tree, 23) == "preprocessor '#region' | preprocessor 'Helpers'");
	CHECK(DescribeRow(tree, 24) == "preprocessor '#endregion'");
}

TEST_CASE("Every bracket and semicolon is a token of its own, so braces can be counted")
{
	Graphite::SyntaxTree tree(Graphite::CSharpGrammar());
	ParsedFixture(tree);

	CHECK(DescribeRow(tree, 21) == "keyword 'void' | function 'Trace' | punctuation '(' | punctuation ')' | punctuation '{' | punctuation '}'");
}

TEST_CASE("A row's tokens ascend and never overlap")
{
	Graphite::SyntaxTree tree(Graphite::CSharpGrammar());
	ParsedFixture(tree);

	for (std::size_t row = 0; row < tree.GetRowCount(); ++row)
	{
		std::vector<Graphite::Token> tokens;
		tree.AppendRowTokens(row, tokens);

		INFO("row ", row);
		CHECK(AreAscendingAndApart(tokens, tree.RowText(row).size()));
	}
}

TEST_CASE("An edited text colours exactly as a fresh parse of it")
{
	const std::string original(kCSharpFixture);
	std::string typedInMiddle = original;
	typedInMiddle.insert(original.find("count = size;"), "x");
	std::string farApartEdits = typedInMiddle;
	farApartEdits.replace(farApartEdits.find("Demo"), 4, "Other");
	farApartEdits.replace(farApartEdits.find("#endregion"), 10, "// done");
	const std::string commentedOut = "/*" + original + "*/";

	Graphite::SyntaxTree tree(Graphite::CSharpGrammar());
	ParsedFixture(tree);

	for (const std::string& text : {typedInMiddle, farApartEdits, original, commentedOut, std::string("int x;"), std::string(), original})
	{
		tree.Retarget(text);
		REQUIRE(tree.AdvanceToEnd());

		INFO("text begins: ", text.substr(0, 40));
		CHECK(DescribeEveryRow(tree) == DescribeFreshParse(text));
	}
}

TEST_CASE("A line broken or joined in the middle of the text colours exactly as a fresh parse of it")
{
	const std::string original(kCSharpFixture);

	std::string enterInMethod = original;
	enterInMethod.insert(original.find("count = size;"), "\n");
	std::string enterInComment = original;
	enterInComment.insert(original.find("ti\n   line */"), "\n");
	std::string enterAtLineEnd = original;
	enterAtLineEnd.insert(original.find("\n    public void Run"), "\n");
	std::string backspaceJoiningMethods = original;
	backspaceJoiningMethods.erase(original.find("\n    public void Run"), 1);
	std::string backspaceJoiningDirective = original;
	backspaceJoiningDirective.erase(original.find("\n    void Trace"), 1);

	Graphite::SyntaxTree tree(Graphite::CSharpGrammar());

	for (const std::string& text : {enterInMethod, enterInComment, enterAtLineEnd, backspaceJoiningMethods, backspaceJoiningDirective})
	{
		ParsedFixture(tree);
		tree.Retarget(text);
		REQUIRE(tree.AdvanceToEnd());

		INFO("edited text: ", text);
		CHECK(DescribeEveryRow(tree) == DescribeFreshParse(text));
	}
}

TEST_CASE("A parse that runs out of time resumes where it stopped")
{
	const std::string text = ManyCSharpClasses(2000);
	Graphite::SyntaxTree tree(Graphite::CSharpGrammar());

	tree.Retarget(text);
	CHECK_FALSE(tree.Advance(std::chrono::microseconds{0}));
	CHECK(tree.IsParsing());
	CHECK_FALSE(tree.HasTree());

	std::size_t attempts = 1;

	while (!tree.Advance(std::chrono::microseconds{0}) && attempts < 1'000'000)
	{
		++attempts;
	}

	CHECK_FALSE(tree.IsParsing());
	CHECK(DescribeRow(tree, 5) == "keyword 'class' | type 'C5' | punctuation '{' | keyword 'int' | identifier 'f' | punctuation '=' | number '1' | punctuation ';' | keyword 'void' | function 'M' | punctuation '(' | punctuation ')' | punctuation '{' | identifier 'f' | punctuation '+=' | number '2' | punctuation ';' | punctuation '}' | punctuation '}'");
}

TEST_CASE("Retargeting in the middle of a parse starts over from the last finished tree")
{
	Graphite::SyntaxTree tree(Graphite::CSharpGrammar());
	ParsedFixture(tree);

	tree.Retarget(ManyCSharpClasses(2000));
	CHECK_FALSE(tree.Advance(std::chrono::microseconds{0}));
	CHECK(DescribeRow(tree, 5) == "keyword 'public' | keyword 'sealed' | keyword 'class' | type 'Widget' | punctuation '<' | type 'T' | punctuation '>' | punctuation ':' | type 'IDisposable' | keyword 'where' | type 'T' | punctuation ':' | keyword 'class'");

	const std::string finalText = "int s = 1;\n" + ManyCSharpClasses(3);
	tree.Retarget(finalText);
	REQUIRE(tree.AdvanceToEnd());

	CHECK(DescribeEveryRow(tree) == DescribeFreshParse(finalText));
}

TEST_CASE("An unterminated string or comment colours without reaching past its row")
{
	Graphite::SyntaxTree tree(Graphite::CSharpGrammar());
	tree.Retarget("string s = \"abc\n/* open\nint x;\n");
	REQUIRE(tree.AdvanceToEnd());

	CHECK(DescribeRow(tree, 0) == "keyword 'string' | identifier 's' | punctuation '=' | string '\"abc'");

	for (std::size_t row = 0; row < tree.GetRowCount(); ++row)
	{
		std::vector<Graphite::Token> tokens;
		tree.AppendRowTokens(row, tokens);

		INFO("row ", row);
		CHECK(AreAscendingAndApart(tokens, tree.RowText(row).size()));
	}

	CHECK_FALSE(DescribeRow(tree, 2).empty());
}

TEST_CASE("Non-ASCII text keeps byte offsets, and a byte that is not UTF-8 does not stop the line")
{
	Graphite::SyntaxTree tree(Graphite::CSharpGrammar());
	tree.Retarget("string s = \"zażółć\";\nvar \xff = 1;\n");
	REQUIRE(tree.AdvanceToEnd());

	CHECK(DescribeRow(tree, 0) == "keyword 'string' | identifier 's' | punctuation '=' | string '\"zażółć\"' | punctuation ';'");
	CHECK(DescribeRow(tree, 1) == "identifier 'var' | punctuation '=' | number '1' | punctuation ';'");
}

TEST_CASE("A text above the size limit gets no tree and no tokens")
{
	Graphite::SyntaxTree tree(Graphite::CSharpGrammar());
	ParsedFixture(tree);

	tree.Retarget(std::string(Graphite::kLargestParsedText + 1, ' '));

	CHECK(tree.AdvanceToEnd());
	CHECK_FALSE(tree.HasTree());
	CHECK(tree.GetRowCount() == 0);
}

TEST_CASE("A grammar with no language is refused, so no parse is left pending")
{
	const Graphite::Grammar grammar(nullptr, "(identifier) @variable");
	Graphite::SyntaxTree tree(grammar);

	tree.Retarget("class C { }");

	CHECK_FALSE(tree.IsParsing());
	CHECK(tree.AdvanceToEnd());
	CHECK_FALSE(tree.HasTree());
}

TEST_CASE("An empty text has one empty row with nothing on it")
{
	Graphite::SyntaxTree tree(Graphite::CSharpGrammar());
	tree.Retarget(std::string());
	REQUIRE(tree.AdvanceToEnd());

	CHECK(tree.GetRowCount() == 1);
	CHECK(DescribeRow(tree, 0).empty());
	CHECK(DescribeRow(tree, 1).empty());
}

TEST_CASE("A generic method call colours as a function and a return type as a type")
{
	const std::vector<std::string> rows = DescribeFreshParse("class Builder\n"
															 "{\n"
															 "    BuiltFloor Build(Spawner spawner)\n"
															 "    {\n"
															 "        var room = spawner.GetComponent<Room>();\n"
															 "        return Find<BuiltFloor>();\n"
															 "    }\n"
															 "}\n");

	REQUIRE(rows.size() >= 6);
	CAPTURE(rows[2]);
	CHECK(rows[2].find("type 'BuiltFloor' | function 'Build'") != std::string::npos);
	CAPTURE(rows[4]);
	CHECK(rows[4].find("function 'GetComponent' | punctuation '<' | type 'Room'") != std::string::npos);
	CAPTURE(rows[5]);
	CHECK(rows[5].find("function 'Find' | punctuation '<' | type 'BuiltFloor'") != std::string::npos);
}
