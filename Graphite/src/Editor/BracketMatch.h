#pragma once

#include <cstddef>
#include <optional>

namespace Core {

	class TextBuffer;
}

namespace Graphite {

	class SyntaxCache;

	inline constexpr std::size_t kBracketScanLines = 500;

	struct BracketPair
	{
		std::size_t Open = 0;
		std::size_t Close = 0;
		bool Matched = false;
	};

	std::optional<BracketPair> FindBracketPair(const Core::TextBuffer& buffer,
											   SyntaxCache& syntax, std::size_t caret);
}
