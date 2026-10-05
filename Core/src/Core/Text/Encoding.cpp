#include "corepch.h"

#include "Core/Text/Encoding.h"

#include "Core/Text/Utf8.h"

#include <algorithm>

namespace Core {

	namespace {
		unsigned char ByteAt(std::string_view bytes, std::size_t offset)
		{
			return static_cast<unsigned char>(bytes[offset]);
		}

		bool IsContinuation(unsigned char byte)
		{
			return (byte & 0xC0u) == 0x80u;
		}
	}

	std::size_t BomLength(std::string_view bytes)
	{
		return BomLength(DetectEncoding(bytes));
	}

	std::size_t BomLength(Encoding encoding)
	{
		// No default: fatalwarnings turns a missing case here into a build error the moment
		// a new encoding is added.
		switch (encoding)
		{
		case Encoding::Utf8Bom:
			return 3;
		case Encoding::Utf16Le:
		case Encoding::Utf16Be:
			return 2;
		case Encoding::Utf8:
		case Encoding::Unknown:
			break;
		}

		return 0;
	}

	std::string_view BomBytes(Encoding encoding)
	{
		switch (encoding)
		{
		case Encoding::Utf8Bom:
			return "\xEF\xBB\xBF";
		case Encoding::Utf16Le:
			return std::string_view{"\xFF\xFE", 2};
		case Encoding::Utf16Be:
			return std::string_view{"\xFE\xFF", 2};
		case Encoding::Utf8:
		case Encoding::Unknown:
			break;
		}

		return {};
	}

	std::string_view WithoutUtf8ByteOrderMark(std::string_view text)
	{
		const std::string_view mark = BomBytes(Encoding::Utf8Bom);

		if (text.starts_with(mark))
		{
			text.remove_prefix(mark.size());
		}

		return text;
	}

	Encoding DetectEncoding(std::string_view bytes)
	{
		if (bytes.size() >= 3 && ByteAt(bytes, 0) == 0xEFu && ByteAt(bytes, 1) == 0xBBu && ByteAt(bytes, 2) == 0xBFu)
		{
			return Encoding::Utf8Bom;
		}

		if (bytes.size() >= 2 && ByteAt(bytes, 0) == 0xFFu && ByteAt(bytes, 1) == 0xFEu)
		{
			return Encoding::Utf16Le;
		}

		if (bytes.size() >= 2 && ByteAt(bytes, 0) == 0xFEu && ByteAt(bytes, 1) == 0xFFu)
		{
			return Encoding::Utf16Be;
		}

		return Encoding::Utf8;
	}

	bool IsValidUtf8(std::string_view bytes)
	{
		std::size_t position = 0;

		while (position < bytes.size())
		{
			if (!DecodeUtf8(bytes, position))
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

		while (next < bytes.size() && IsContinuation(ByteAt(bytes, next)))
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

		std::size_t previous = (offset > bytes.size() ? bytes.size() : offset) - 1;

		while (previous > 0 && IsContinuation(ByteAt(bytes, previous)))
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