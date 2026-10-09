#include <doctest/doctest.h>

#include "Language/Api/TokenKind.h"

using Graphite::NamesSomething;
using Graphite::TokenKind;

TEST_CASE("An identifier and every kind a server or grammar gives a name are names")
{
	CHECK(NamesSomething(TokenKind::Identifier));
	CHECK(NamesSomething(TokenKind::SemanticType));
	CHECK(NamesSomething(TokenKind::SemanticFunction));
	CHECK(NamesSomething(TokenKind::SemanticMacro));
	CHECK(NamesSomething(TokenKind::SemanticMember));
	CHECK(NamesSomething(TokenKind::SemanticParameter));
	CHECK(NamesSomething(TokenKind::SemanticNamespace));
}

TEST_CASE("Keywords, literals, comments, directives and punctuation are not names")
{
	CHECK_FALSE(NamesSomething(TokenKind::Text));
	CHECK_FALSE(NamesSomething(TokenKind::Keyword));
	CHECK_FALSE(NamesSomething(TokenKind::Number));
	CHECK_FALSE(NamesSomething(TokenKind::String));
	CHECK_FALSE(NamesSomething(TokenKind::Character));
	CHECK_FALSE(NamesSomething(TokenKind::Comment));
	CHECK_FALSE(NamesSomething(TokenKind::Preprocessor));
	CHECK_FALSE(NamesSomething(TokenKind::Punctuation));
	CHECK_FALSE(NamesSomething(TokenKind::Invalid));
}
