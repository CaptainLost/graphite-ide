#include "corepch.h"

#include "Core/Text/WordBounds.h"

#include "Core/Text/Utf8.h"

#include <cctype>
#include <optional>

namespace Core {

	namespace {
		bool IsContinuation(char byte)
		{
			return (static_cast<unsigned char>(byte) & 0xC0) == 0x80;
		}

		// First byte of the character `byte` falls inside.
		std::size_t CharacterStart(std::string_view line, std::size_t byte)
		{
			while (byte > 0 && IsContinuation(line[byte]))
			{
				--byte;
			}
			return byte;
		}

		// A character that does not decode is not a word character and is one byte wide.
		struct Character
		{
			char32_t CodePoint = 0;
			std::size_t End = 0;
			bool Valid = false;
		};

		Character ReadCharacter(std::string_view line, std::size_t start)
		{
			std::size_t offset = start;
			const std::optional<char32_t> decoded = DecodeUtf8(line, offset);
			return Character{decoded.value_or(0), offset, decoded.has_value()};
		}

		bool IsWordCharacter(const Character& character, WordKind kind)
		{
			return character.Valid && IsWordCodePoint(character.CodePoint, kind);
		}
	}

	bool IsWordCodePoint(char32_t codePoint, WordKind kind)
	{
		if (codePoint >= 0x80)
		{
			return true;
		}

		const char ascii = static_cast<char>(codePoint);

		if (std::isalnum(static_cast<unsigned char>(ascii)) != 0 || ascii == '_')
		{
			return true;
		}

		if (kind == WordKind::Path)
		{
			return ascii == '/' || ascii == '\\' || ascii == '.' || ascii == '-' || ascii == ':' ||
				   ascii == '~';
		}

		return false;
	}

	ColumnSpan WordSpanAt(std::string_view line, std::size_t byte, WordKind kind)
	{
		if (byte >= line.size())
		{
			return ColumnSpan{line.size(), line.size()};
		}

		const std::size_t start = CharacterStart(line, byte);
		const Character here = ReadCharacter(line, start);

		if (!IsWordCharacter(here, kind))
		{
			return ColumnSpan{start, here.End};
		}

		std::size_t from = start;
		while (from > 0)
		{
			const std::size_t previous = CharacterStart(line, from - 1);

			if (!IsWordCharacter(ReadCharacter(line, previous), kind))
			{
				break;
			}

			from = previous;
		}

		std::size_t to = here.End;
		while (to < line.size())
		{
			const Character next = ReadCharacter(line, to);

			if (!IsWordCharacter(next, kind))
			{
				break;
			}

			to = next.End;
		}

		return ColumnSpan{from, to};
	}
}
