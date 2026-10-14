#pragma once

#include "Language/Api/LanguageService.h"
#include "Language/Api/TokenKind.h"
#include "Language/Syntax/Token.h"

#include <cstddef>
#include <functional>
#include <map>
#include <vector>

namespace Core {

	class TextBuffer;
}

namespace Graphite {

	struct SemanticSpan
	{
		std::size_t From = 0;
		std::size_t To = 0;
		TokenKind Kind = TokenKind::Text;
	};

	class SemanticOverlay
	{
	public:
		void Set(const std::vector<SemanticToken>& tokens, const Core::TextBuffer& buffer,
				 std::size_t tabWidth);

		void Clear();

		bool IsEmpty() const { return m_Lines.empty(); }

		std::size_t Footprint() const;

		void ForEachLine(
			const std::function<void(std::size_t, const std::vector<SemanticSpan>&)>& visit) const;

		void SetSpansOn(std::size_t line, std::vector<SemanticSpan> spans);

		const std::vector<SemanticSpan>& SpansOn(std::size_t line) const;

	private:
		std::map<std::size_t, std::vector<SemanticSpan>> m_Lines;
	};

	TokenKind ColourOf(const Token& token, const std::vector<SemanticSpan>& semantic);
}
