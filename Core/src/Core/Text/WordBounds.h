#pragma once

#include "Core/Text/TextSelection.h"

#include <cstddef>
#include <string_view>

namespace Core {

	enum class WordKind
	{
		// Letters, digits, '_' and every non-ASCII code point.
		Code,
		// Code, plus the characters that keep a file path and its line suffix whole: / \ . - : ~
		Path
	};

	bool IsWordCodePoint(char32_t codePoint, WordKind kind);

	// Byte span of the word around `byte` in UTF-8 `line`. A byte on a non-word character selects
	// that one character; past the end selects nothing (an empty span at line.size()).
	ColumnSpan WordSpanAt(std::string_view line, std::size_t byte, WordKind kind);
}
