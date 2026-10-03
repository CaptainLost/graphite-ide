#pragma once

#include "Editor/SemanticOverlay.h"

#include "Lang/Syntax/Token.h"

#include <string_view>
#include <vector>

namespace Graphite {

	// The colour a name of clangd's hover kind ("instance method", "class", "field"...) takes in
	// the editor: the same mapping its semantic tokens go through. Text for a kind the editor
	// leaves in the lexer's colour, a local variable among them.
	TokenKind KindOfHoverSymbol(std::string_view hoverKind);

	// What a server would have said about the identifiers in a hover's declaration line, which no
	// server colours: the declared `name` as `nameKind`, a function's parameter names as
	// parameters, every other identifier the declaration spells a type with as a type, and a name
	// qualifying one (`std` in `std::string`) as a namespace. A default argument and an
	// initialiser are left to the lexer — they are expressions, and an identifier in one could be
	// anything. `tokens` are `text`'s.
	std::vector<SemanticSpan> DeclarationSpans(std::string_view text,
											   const std::vector<Token>& tokens,
											   std::string_view name, TokenKind nameKind);

	// The same for text that spells a type alone, as a return type does: its identifiers as types,
	// including those of the " (aka ...)" clangd may follow it with.
	std::vector<SemanticSpan> TypeSpans(std::string_view text, const std::vector<Token>& tokens);

	// The same for one parameter, as clangd lists it ("const Event & event", "bool force =
	// false"): the name as a parameter, what spells its type as types.
	std::vector<SemanticSpan> ParameterSpans(std::string_view text,
											 const std::vector<Token>& tokens);
}
