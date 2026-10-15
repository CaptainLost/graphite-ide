#pragma once

#include "Core/Text/TextSelection.h"

#include <cstddef>
#include <string_view>

namespace Core {

	enum class WordKind
	{
		Code,
		Path
	};

	bool IsWordCodePoint(char32_t codePoint, WordKind kind);

	// For code scanned byte by byte: every byte of a character beyond ASCII counts, as that character
	// does in IsWordCodePoint.
	bool IsWordByte(char byte);

	// The span is in bytes of the UTF-8 line, not in display columns, although the type is ColumnSpan.
	ColumnSpan WordSpanAt(std::string_view lineText, std::size_t byteOffset, WordKind kind);
}
