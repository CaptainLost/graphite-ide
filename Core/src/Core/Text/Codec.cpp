#include "corepch.h"

#include "Core/Text/Codec.h"

#include <cstdint>
#include <string>

namespace Core {

	namespace {
		constexpr std::uint32_t kHighSurrogateFirst = 0xD800;
		constexpr std::uint32_t kHighSurrogateLast = 0xDBFF;
		constexpr std::uint32_t kLowSurrogateFirst = 0xDC00;
		constexpr std::uint32_t kLowSurrogateLast = 0xDFFF;
		constexpr std::uint32_t kFirstSupplementary = 0x10000;

		std::uint32_t ReadUnit(std::string_view bytes, std::size_t at,
							   bool littleEndian)
		{
			const auto first = static_cast<std::uint32_t>(
				static_cast<unsigned char>(bytes[at]));
			const auto second = static_cast<std::uint32_t>(
				static_cast<unsigned char>(bytes[at + 1]));

			return littleEndian ? (second << 8) | first : (first << 8) | second;
		}

		void AppendUnit(std::string& out, std::uint32_t unit, bool littleEndian)
		{
			const auto low = static_cast<char>(unit & 0xFFu);
			const auto high = static_cast<char>((unit >> 8) & 0xFFu);

			out.push_back(littleEndian ? low : high);
			out.push_back(littleEndian ? high : low);
		}

		void AppendUtf8(std::string& out, std::uint32_t codepoint)
		{
			if (codepoint < 0x80u)
			{
				out.push_back(static_cast<char>(codepoint));
			}
			else if (codepoint < 0x800u)
			{
				out.push_back(static_cast<char>(0xC0u | (codepoint >> 6)));
				out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
			}
			else if (codepoint < kFirstSupplementary)
			{
				out.push_back(static_cast<char>(0xE0u | (codepoint >> 12)));
				out.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu)));
				out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
			}
			else
			{
				out.push_back(static_cast<char>(0xF0u | (codepoint >> 18)));
				out.push_back(static_cast<char>(0x80u | ((codepoint >> 12) & 0x3Fu)));
				out.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu)));
				out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
			}
		}

		Result<std::string> DecodeUtf16(std::string_view bytes,
										bool littleEndian)
		{
			if (bytes.size() % 2 != 0)
			{
				return Error{ErrorCode::InvalidArgument,
							 "UTF-16 file is damaged: odd number of bytes (" + std::to_string(bytes.size()) + ")"};
			}

			std::string out;
			out.reserve(bytes.size());

			std::size_t at = 0;

			while (at < bytes.size())
			{
				const std::uint32_t unit = ReadUnit(bytes, at, littleEndian);

				if (unit >= kLowSurrogateFirst && unit <= kLowSurrogateLast)
				{
					return Error{ErrorCode::InvalidArgument,
								 "UTF-16 file is damaged: a low surrogate at byte " + std::to_string(at) + " has no high surrogate "
																										   "before it"};
				}

				if (unit >= kHighSurrogateFirst && unit <= kHighSurrogateLast)
				{
					if (at + 4 > bytes.size())
					{
						return Error{ErrorCode::InvalidArgument,
									 "UTF-16 file is damaged: a high surrogate at byte " + std::to_string(at) + " is the last unit"};
					}

					const std::uint32_t low = ReadUnit(bytes, at + 2, littleEndian);

					if (low < kLowSurrogateFirst || low > kLowSurrogateLast)
					{
						return Error{ErrorCode::InvalidArgument,
									 "UTF-16 file is damaged: a high surrogate at byte " + std::to_string(at) + " is not followed by a low one"};
					}

					AppendUtf8(out, kFirstSupplementary + ((unit - kHighSurrogateFirst) << 10) + (low - kLowSurrogateFirst));
					at += 4;
					continue;
				}

				AppendUtf8(out, unit);
				at += 2;
			}

			return out;
		}

		std::string EncodeUtf16(std::string_view utf8, bool littleEndian)
		{
			std::string out;
			out.reserve(utf8.size() * 2);

			std::size_t at = 0;

			while (at < utf8.size())
			{
				const auto lead = static_cast<unsigned char>(utf8[at]);

				std::size_t length = 1;
				std::uint32_t codepoint = lead;

				if ((lead & 0xE0u) == 0xC0u)
				{
					length = 2;
					codepoint = lead & 0x1Fu;
				}
				else if ((lead & 0xF0u) == 0xE0u)
				{
					length = 3;
					codepoint = lead & 0x0Fu;
				}
				else if ((lead & 0xF8u) == 0xF0u)
				{
					length = 4;
					codepoint = lead & 0x07u;
				}

				for (std::size_t index = 1; index < length && at + index < utf8.size();
					 ++index)
				{
					codepoint = (codepoint << 6) | (static_cast<unsigned char>(utf8[at + index]) & 0x3Fu);
				}

				if (codepoint < kFirstSupplementary)
				{
					AppendUnit(out, codepoint, littleEndian);
				}
				else
				{
					const std::uint32_t offset = codepoint - kFirstSupplementary;

					AppendUnit(out, kHighSurrogateFirst + (offset >> 10), littleEndian);
					AppendUnit(out, kLowSurrogateFirst + (offset & 0x3FFu), littleEndian);
				}

				at += length;
			}

			return out;
		}

		enum class Transform
		{
			PassThrough,
			Utf16Little,
			Utf16Big
		};

		Transform CodecFor(Encoding encoding)
		{
			// No default: adding an Encoding enumerator without deciding its Transform here
			// is then a build error.
			switch (encoding)
			{
			case Encoding::Utf16Le:
				return Transform::Utf16Little;
			case Encoding::Utf16Be:
				return Transform::Utf16Big;
			case Encoding::Utf8:
			case Encoding::Utf8Bom:
			case Encoding::Unknown:
				break;
			}

			return Transform::PassThrough;
		}

	}

	bool LooksLikeText(std::string_view utf8)
	{
		// Byte value alone is not enough: DEL (U+007F) is a single byte >= 0x20, and the C1
		// controls (U+0080-U+009F) encode as C2 80 through C2 9F, whose bytes are also
		// >= 0x20, so both are matched by the exact sequences that encode them.
		for (std::size_t at = 0; at < utf8.size(); ++at)
		{
			const auto byte = static_cast<unsigned char>(utf8[at]);

			if (byte == 0x7Fu)
				return false;

			if (byte == 0xC2u && at + 1 < utf8.size())
			{
				const auto next = static_cast<unsigned char>(utf8[at + 1]);

				if (next >= 0x80u && next <= 0x9Fu)
					return false;
			}

			if (byte >= 0x20u || byte == '\t' || byte == '\n' || byte == '\r')
				continue;

			return false;
		}

		return true;
	}

	Result<std::string> Decode(std::string_view bytes, Encoding encoding)
	{
		switch (CodecFor(encoding))
		{
		case Transform::Utf16Little:
			return DecodeUtf16(bytes, true);
		case Transform::Utf16Big:
			return DecodeUtf16(bytes, false);
		case Transform::PassThrough:
			break;
		}

		return std::string{bytes};
	}

	std::string Encode(std::string_view utf8, Encoding encoding)
	{
		switch (CodecFor(encoding))
		{
		case Transform::Utf16Little:
			return EncodeUtf16(utf8, true);
		case Transform::Utf16Big:
			return EncodeUtf16(utf8, false);
		case Transform::PassThrough:
			break;
		}

		return std::string{utf8};
	}

	bool TransformsContent(Encoding encoding)
	{
		return CodecFor(encoding) != Transform::PassThrough;
	}

	Encoding SniffEncoding(std::string_view bytes)
	{
		const Encoding declared = DetectEncoding(bytes);

		// A mark is a statement by whoever wrote the file; nothing here overrules it.
		if (declared != Encoding::Utf8)
			return declared;

		std::size_t evenNuls = 0;
		std::size_t oddNuls = 0;

		for (std::size_t at = 0; at < bytes.size(); ++at)
		{
			if (bytes[at] != '\0')
				continue;

			if (at % 2 == 0)
				++evenNuls;
			else
				++oddNuls;
		}

		const std::size_t nuls = evenNuls + oddNuls;

		if (nuls < 2)
			return Encoding::Utf8;

		const std::size_t dominant = evenNuls > oddNuls ? evenNuls : oddNuls;

		// Nine tenths rather than all of them: U+0100 and its neighbours put a NUL at the
		// other parity, and a file may legitimately contain a few.
		if (dominant * 10 < nuls * 9)
			return Encoding::Utf8;

		// A NUL at an odd position means the low byte came first.
		const Encoding candidate = oddNuls > evenNuls ? Encoding::Utf16Le
													  : Encoding::Utf16Be;

		// A sample may end mid-unit; that is a boundary, not damage.
		const std::string_view whole = bytes.substr(0, bytes.size() - bytes.size() % 2);
		const auto decoded = Decode(whole, candidate);

		if (!decoded)
			return Encoding::Utf8;

		return LooksLikeText(decoded.GetValue()) ? candidate : Encoding::Utf8;
	}
}