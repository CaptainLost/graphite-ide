#pragma once

#include "Editor/Overlays/SemanticOverlay.h"

#include "Language/Syntax/Token.h"

#include <string_view>
#include <vector>

namespace Graphite {

	TokenKind KindOfHoverSymbol(std::string_view hoverKind);

	std::vector<SemanticSpan> DeclarationSpans(std::string_view text,
											   const std::vector<Token>& tokens,
											   std::string_view name, TokenKind nameKind);

	std::vector<SemanticSpan> TypeSpans(std::string_view text, const std::vector<Token>& tokens);

	std::vector<SemanticSpan> ParameterSpans(std::string_view text,
											 const std::vector<Token>& tokens);
}
