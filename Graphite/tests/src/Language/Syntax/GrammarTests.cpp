#include <doctest/doctest.h>

#include "Language/Syntax/Grammar.h"
#include "Language/Syntax/Grammars/CSharpGrammar.h"

#include <tree_sitter/api.h>

#include <cstdint>
#include <string>

extern "C" const TSLanguage* tree_sitter_c_sharp();

using Graphite::KindOfCaptureName;
using Graphite::TokenKind;

TEST_CASE("A capture name takes the kind of the rule it names or falls under")
{
	CHECK(KindOfCaptureName("keyword") == TokenKind::Keyword);
	CHECK(KindOfCaptureName("keyword.control") == TokenKind::Keyword);
	CHECK(KindOfCaptureName("string") == TokenKind::String);
	CHECK(KindOfCaptureName("string.escape") == TokenKind::String);
	CHECK(KindOfCaptureName("character") == TokenKind::Character);
	CHECK(KindOfCaptureName("comment") == TokenKind::Comment);
	CHECK(KindOfCaptureName("number") == TokenKind::Number);
	CHECK(KindOfCaptureName("type") == TokenKind::SemanticType);
	CHECK(KindOfCaptureName("type.builtin") == TokenKind::SemanticType);
	CHECK(KindOfCaptureName("function") == TokenKind::SemanticFunction);
	CHECK(KindOfCaptureName("property") == TokenKind::SemanticMember);
	CHECK(KindOfCaptureName("namespace") == TokenKind::SemanticNamespace);
	CHECK(KindOfCaptureName("module") == TokenKind::SemanticNamespace);
	CHECK(KindOfCaptureName("preprocessor") == TokenKind::Preprocessor);
	CHECK(KindOfCaptureName("operator") == TokenKind::Punctuation);
	CHECK(KindOfCaptureName("punctuation.bracket") == TokenKind::Punctuation);
}

TEST_CASE("A parameter is told apart from the variables its name would otherwise fall under")
{
	CHECK(KindOfCaptureName("variable.parameter") == TokenKind::SemanticParameter);
	CHECK(KindOfCaptureName("variable") == TokenKind::Identifier);
	CHECK(KindOfCaptureName("variable.builtin") == TokenKind::Identifier);
}

TEST_CASE("A capture name that only begins like a rule takes no kind")
{
	CHECK_FALSE(KindOfCaptureName("keywords").has_value());
	CHECK_FALSE(KindOfCaptureName("attribute").has_value());
	CHECK_FALSE(KindOfCaptureName("").has_value());
}

TEST_CASE("The C# highlight query compiles and every capture in it has a kind")
{
	const Graphite::Grammar& grammar = Graphite::CSharpGrammar();

	INFO("query error at byte ", grammar.GetQueryErrorOffset().value_or(0));
	REQUIRE(grammar.GetHighlights() != nullptr);
	CHECK(grammar.CaptureCount() > 0);

	for (std::uint32_t captureIndex = 0; captureIndex < grammar.CaptureCount(); ++captureIndex)
	{
		INFO("capture @", std::string(grammar.CaptureName(captureIndex)));
		CHECK(grammar.KindOfCapture(captureIndex).has_value());
	}
}

TEST_CASE("A query naming a node the grammar lacks leaves the grammar without highlights")
{
	const Graphite::Grammar broken(tree_sitter_c_sharp(), "(no_such_node) @keyword");

	CHECK(broken.GetHighlights() == nullptr);
	CHECK(broken.GetQueryErrorOffset().has_value());
	CHECK(broken.CaptureCount() == 0);
	CHECK_FALSE(broken.KindOfCapture(0).has_value());
}
