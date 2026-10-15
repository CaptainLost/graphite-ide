#include "corepch.h"

#include "Core/Text/WordBounds.h"

#include "Core/Text/Encoding.h"
#include "Core/Text/Utf8.h"

#include <cctype>
#include <optional>

namespace Core {

	namespace {
		constexpr char32_t kFirstCodePointOutsideAscii = 0x80;
		constexpr char32_t kCodePointOfUndecodedCharacter = 0;
		constexpr std::string_view kFilePathAndLineSuffixPunctuation = "/\\.-:~";

		std::size_t StartOfCharacterContaining(std::string_view lineText, std::size_t byteOffset)
		{
			while (byteOffset > 0 && IsUtf8ContinuationByte(lineText[byteOffset]))
			{
				--byteOffset;
			}
			return byteOffset;
		}

		struct Character
		{
			char32_t CodePoint = 0;
			std::size_t End = 0;
			bool Decoded = false;
		};

		Character ReadCharacter(std::string_view lineText, std::size_t characterStart)
		{
			std::size_t characterEnd = characterStart;
			const std::optional<char32_t> codePoint = DecodeUtf8(lineText, characterEnd);
			return Character{.CodePoint = codePoint.value_or(kCodePointOfUndecodedCharacter), .End = characterEnd, .Decoded = codePoint.has_value()};
		}

		bool IsWordCharacter(const Character& character, WordKind kind)
		{
			return character.Decoded && IsWordCodePoint(character.CodePoint, kind);
		}

		bool IsFilePathOrLineSuffixPunctuation(char asciiCharacter)
		{
			return kFilePathAndLineSuffixPunctuation.find(asciiCharacter) != std::string_view::npos;
		}
	}

	bool IsWordCodePoint(char32_t codePoint, WordKind kind)
	{
		if (codePoint >= kFirstCodePointOutsideAscii)
		{
			return true;
		}

		const char asciiCharacter = static_cast<char>(codePoint);
		const bool isLetterOrDigit = std::isalnum(static_cast<unsigned char>(asciiCharacter)) != 0;

		if (isLetterOrDigit || asciiCharacter == '_')
		{
			return true;
		}

		if (kind == WordKind::Path)
		{
			return IsFilePathOrLineSuffixPunctuation(asciiCharacter);
		}

		return false;
	}

	bool IsWordByte(char byte)
	{
		const auto value = static_cast<unsigned char>(byte);

		return value >= kFirstCodePointOutsideAscii || IsWordCodePoint(value, WordKind::Code);
	}

	ColumnSpan WordSpanAt(std::string_view lineText, std::size_t byteOffset, WordKind kind)
	{
		if (byteOffset >= lineText.size())
		{
			return ColumnSpan{.From = lineText.size(), .To = lineText.size()};
		}

		const std::size_t characterStart = StartOfCharacterContaining(lineText, byteOffset);
		const Character characterAtOffset = ReadCharacter(lineText, characterStart);

		if (!IsWordCharacter(characterAtOffset, kind))
		{
			return ColumnSpan{.From = characterStart, .To = characterAtOffset.End};
		}

		std::size_t wordStart = characterStart;
		while (wordStart > 0)
		{
			const std::size_t previousCharacterStart = StartOfCharacterContaining(lineText, wordStart - 1);
			const Character previousCharacter = ReadCharacter(lineText, previousCharacterStart);

			if (!IsWordCharacter(previousCharacter, kind))
			{
				break;
			}

			wordStart = previousCharacterStart;
		}

		std::size_t wordEnd = characterAtOffset.End;
		while (wordEnd < lineText.size())
		{
			const Character nextCharacter = ReadCharacter(lineText, wordEnd);

			if (!IsWordCharacter(nextCharacter, kind))
			{
				break;
			}

			wordEnd = nextCharacter.End;
		}

		return ColumnSpan{.From = wordStart, .To = wordEnd};
	}
}
