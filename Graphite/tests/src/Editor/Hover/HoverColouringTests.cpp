#include <doctest/doctest.h>

#include "Editor/Hover/HoverColouring.h"

#include "SyntaxFixtures.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace Graphite;

namespace {

	using Coloured = std::vector<std::pair<std::string, TokenKind>>;

	Coloured Words(std::string_view text, std::vector<SemanticSpan> spans)
	{
		std::ranges::sort(spans, {}, &SemanticSpan::From);

		Coloured words;

		for (const SemanticSpan& span : spans)
		{
			words.emplace_back(std::string(text.substr(span.From, span.To - span.From)), span.Kind);
		}

		return words;
	}

	Coloured DeclarationWords(std::string_view text, std::string_view name, TokenKind kind)
	{
		return Words(text, DeclarationSpans(text, LexCppLine(text), name, kind));
	}

	Coloured ParameterWords(std::string_view text)
	{
		return Words(text, ParameterSpans(text, LexCppLine(text)));
	}

	constexpr TokenKind kType = TokenKind::SemanticType;
	constexpr TokenKind kFunction = TokenKind::SemanticFunction;
	constexpr TokenKind kParameter = TokenKind::SemanticParameter;
	constexpr TokenKind kNamespace = TokenKind::SemanticNamespace;
}

TEST_CASE("A method's declaration colours its name, its parameters and the types it spells")
{
	CHECK(DeclarationWords("void OnEvent(Event& event)", "OnEvent", kFunction) ==
		  Coloured{{"OnEvent", kFunction}, {"Event", kType}, {"event", kParameter}});

	CHECK(DeclarationWords("virtual Ref<Layer> Push(const LayerSpec & spec, bool force = false)",
						   "Push", kFunction) ==
		  Coloured{{"Ref", kType},
				   {"Layer", kType},
				   {"Push", kFunction},
				   {"LayerSpec", kType},
				   {"spec", kParameter},
				   {"force", kParameter}});
}

TEST_CASE("A qualifier is a namespace; a default argument and an array extent are the lexer's")
{
	CHECK(DeclarationWords("std::string Name(std::size_t at = Default, int cells[Count])", "Name",
						   kFunction) == Coloured{{"std", kNamespace},
												  {"string", kType},
												  {"Name", kFunction},
												  {"std", kNamespace},
												  {"size_t", kType},
												  {"at", kParameter},
												  {"cells", kParameter}});
}

TEST_CASE("An unnamed parameter is all type, a template's commas split no parameter")
{
	CHECK(DeclarationWords("void Take(Event&, std::map<Key, Value> table)", "Take", kFunction) ==
		  Coloured{{"Take", kFunction},
				   {"Event", kType},
				   {"std", kNamespace},
				   {"map", kType},
				   {"Key", kType},
				   {"Value", kType},
				   {"table", kParameter}});
}

TEST_CASE("An explicit specialisation's template arguments are types, its parameters still found")
{
	CHECK(DeclarationWords("template <> Scope<Layer> CreateScope<Core::Layer, int &>(Core::Layer & "
						   "args, int & count)",
						   "CreateScope", kFunction) == Coloured{{"Scope", kType},
																 {"Layer", kType},
																 {"CreateScope", kFunction},
																 {"Core", kNamespace},
																 {"Layer", kType},
																 {"Core", kNamespace},
																 {"Layer", kType},
																 {"args", kParameter},
																 {"count", kParameter}});
}

TEST_CASE("The aka clangd follows a type with is a type too, the word itself nothing")
{
	constexpr std::string_view kReturn = "Scope<Layer> (aka std::unique_ptr<Layer>)";

	CHECK(Words(kReturn, TypeSpans(kReturn, LexCppLine(kReturn))) == Coloured{{"Scope", kType},
																			  {"Layer", kType},
																			  {"std", kNamespace},
																			  {"unique_ptr", kType},
																			  {"Layer", kType}});

	CHECK(ParameterWords("std::size_t count (aka unsigned long long)") ==
		  Coloured{{"std", kNamespace}, {"size_t", kType}, {"count", kParameter}});
}

TEST_CASE("A variable's initialiser and a trailing return type")
{
	CHECK(DeclarationWords("Timestep step = Clock::Now()", "step", TokenKind::Text) ==
		  Coloured{{"Timestep", kType}});

	CHECK(DeclarationWords("auto Size() const -> Count", "Size", kFunction) ==
		  Coloured{{"Size", kFunction}, {"Count", kType}});
}

TEST_CASE("A destructor is found by the name after its tilde, an operator not at all")
{
	CHECK(DeclarationWords("~Layer()", "~Layer", kFunction) == Coloured{{"Layer", kFunction}});
	CHECK(DeclarationWords("bool operator==(const Layer & other)", "operator==", kFunction).empty());
}

TEST_CASE("A parameter as clangd lists it")
{
	CHECK(ParameterWords("Event & event") == Coloured{{"Event", kType}, {"event", kParameter}});
	CHECK(ParameterWords("bool force = false") == Coloured{{"force", kParameter}});
	CHECK(ParameterWords("const std::string &") ==
		  Coloured{{"std", kNamespace}, {"string", kType}});
}

TEST_CASE("Hover kinds take the colours the editor gives the same names")
{
	CHECK(KindOfHoverSymbol("instance method") == kFunction);
	CHECK(KindOfHoverSymbol("class") == kType);
	CHECK(KindOfHoverSymbol("field") == TokenKind::SemanticMember);
	CHECK(KindOfHoverSymbol("variable") == TokenKind::Text);
}
