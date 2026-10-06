#include "corepch.h"

#include "Core/Text/Encoding.h"

#include "Core/Text/Utf8.h"

#include <algorithm>

namespace Core {

	namespace {
		constexpr std::string_view kUtf8ByteOrderMark = "\xEF\xBB\xBF";
		constexpr std::string_view kUtf16LittleEndianByteOrderMark = "\xFF\xFE";
		constexpr std::string_view kUtf16BigEndianByteOrderMark = "\xFE\xFF";
	}

	std::size_t LeadingByteOrderMarkLength(std::string_view bytes)
	{
		return ByteOrderMarkLength(DetectEncoding(bytes));
	}

	std::size_t ByteOrderMarkLength(Encoding encoding)
	{
		return ByteOrderMarkBytes(encoding).size();
	}

	std::string_view ByteOrderMarkBytes(Encoding encoding)
	{
		// No default, so that a new encoding missing from this switch breaks the build.
		switch (encoding)
		{
		case Encoding::Utf8Bom:
			return kUtf8ByteOrderMark;
		case Encoding::Utf16Le:
			return kUtf16LittleEndianByteOrderMark;
		case Encoding::Utf16Be:
			return kUtf16BigEndianByteOrderMark;
		case Encoding::Utf8:
		case Encoding::Unknown:
			break;
		}

		return {};
	}

	std::string_view WithoutUtf8ByteOrderMark(std::string_view text)
	{
		const std::string_view mark = ByteOrderMarkBytes(Encoding::Utf8Bom);

		if (text.starts_with(mark))
		{
			text.remove_prefix(mark.size());
		}

		return text;
	}

	Encoding DetectEncoding(std::string_view bytes)
	{
		if (bytes.starts_with(kUtf8ByteOrderMark))
		{
			return Encoding::Utf8Bom;
		}

		if (bytes.starts_with(kUtf16LittleEndianByteOrderMark))
		{
			return Encoding::Utf16Le;
		}

		if (bytes.starts_with(kUtf16BigEndianByteOrderMark))
		{
			return Encoding::Utf16Be;
		}

		return Encoding::Utf8;
	}

	bool IsValidUtf8(std::string_view bytes)
	{
		std::size_t offset = 0;

		while (offset < bytes.size())
		{
			const std::optional<char32_t> decodedCharacter = DecodeUtf8(bytes, offset);

			if (!decodedCharacter)
			{
				return false;
			}
		}

		return true;
	}

	std::size_t NextCharacter(std::string_view bytes, std::size_t offset)
	{
		if (offset >= bytes.size())
		{
			return bytes.size();
		}

		std::size_t next = offset + 1;

		while (next < bytes.size() && IsUtf8ContinuationByte(bytes[next]))
		{
			++next;
		}

		return next;
	}

	std::size_t PreviousCharacter(std::string_view bytes, std::size_t offset)
	{
		if (offset == 0 || bytes.empty())
		{
			return 0;
		}

		const std::size_t offsetWithinText = std::min(offset, bytes.size());
		std::size_t previous = offsetWithinText - 1;

		while (previous > 0 && IsUtf8ContinuationByte(bytes[previous]))
		{
			--previous;
		}

		return previous;
	}

	std::size_t CharacterBoundaryAtOrBefore(std::string_view bytes, std::size_t offset)
	{
		std::size_t boundary = std::min(offset, bytes.size());

		while (boundary > 0 && boundary < bytes.size() && IsUtf8ContinuationByte(bytes[boundary]))
		{
			--boundary;
		}

		return boundary;
	}

	std::size_t CharacterBoundaryAtOrAfter(std::string_view bytes, std::size_t offset)
	{
		std::size_t boundary = offset;

		while (boundary < bytes.size() && IsUtf8ContinuationByte(bytes[boundary]))
		{
			++boundary;
		}

		return boundary;
	}

	std::size_t CharacterCount(std::string_view bytes)
	{
		std::size_t count = 0;

		for (const char byte : bytes)
		{
			if (!IsUtf8ContinuationByte(byte))
			{
				++count;
			}
		}

		return count;
	}

	std::string_view EncodingName(Encoding encoding)
	{
		switch (encoding)
		{
		case Encoding::Utf8:
			return "UTF-8";
		case Encoding::Utf8Bom:
			return "UTF-8 with BOM";
		case Encoding::Utf16Le:
			return "UTF-16 LE";
		case Encoding::Utf16Be:
			return "UTF-16 BE";
		case Encoding::Unknown:
			break;
		}

		return "Unknown";
	}
}
